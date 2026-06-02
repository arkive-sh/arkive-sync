#include "platform/macos/SecureStorage.hpp"

#include <Security/Security.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdexcept>

namespace {

void validateInputs(const std::string &service, const std::string &account,
                    const std::vector<uint8_t> *secret = nullptr) {
  if (service.empty() || account.empty()) {
    throw std::invalid_argument("SecureStorage service/account cannot be empty");
  }
  if (secret != nullptr && secret->empty()) {
    throw std::invalid_argument("SecureStorage secret cannot be empty");
  }
}

std::runtime_error makeKeychainError(const std::string &prefix,
                                     OSStatus status) {
  CFStringRef messageRef = SecCopyErrorMessageString(status, nullptr);
  std::string message = prefix + ": OSStatus " + std::to_string(status);
  if (messageRef != nullptr) {
    char buffer[512] = {};
    if (CFStringGetCString(messageRef, buffer, sizeof(buffer),
                           kCFStringEncodingUTF8)) {
      message += " (" + std::string(buffer) + ")";
    }
    CFRelease(messageRef);
  }
  return std::runtime_error(message);
}

class ScopedCFTypeRef {
public:
  explicit ScopedCFTypeRef(CFTypeRef value = nullptr) : value_(value) {}
  ~ScopedCFTypeRef() {
    if (value_ != nullptr) {
      CFRelease(value_);
    }
  }

  ScopedCFTypeRef(const ScopedCFTypeRef &) = delete;
  ScopedCFTypeRef &operator=(const ScopedCFTypeRef &) = delete;
  ScopedCFTypeRef(ScopedCFTypeRef &&other) noexcept : value_(other.value_) {
    other.value_ = nullptr;
  }
  ScopedCFTypeRef &operator=(ScopedCFTypeRef &&other) noexcept {
    if (this != &other) {
      if (value_ != nullptr) {
        CFRelease(value_);
      }
      value_ = other.value_;
      other.value_ = nullptr;
    }
    return *this;
  }

  CFTypeRef get() const { return value_; }
  operator CFTypeRef() const { return value_; }

private:
  CFTypeRef value_;
};

ScopedCFTypeRef makeCFString(const std::string &value) {
  return ScopedCFTypeRef(CFStringCreateWithBytes(
      kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(value.data()),
      static_cast<CFIndex>(value.size()), kCFStringEncodingUTF8, false));
}

ScopedCFTypeRef makeCFData(const std::vector<uint8_t> &value) {
  return ScopedCFTypeRef(CFDataCreate(
      kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(value.data()),
      static_cast<CFIndex>(value.size())));
}

CFMutableDictionaryRef asMutableDictionary(const ScopedCFTypeRef &ref) {
  return reinterpret_cast<CFMutableDictionaryRef>(
      const_cast<void *>(ref.get()));
}

CFMutableDictionaryRef createBaseQuery(const std::string &service,
                                       const std::string &account) {
  CFMutableDictionaryRef query = CFDictionaryCreateMutable(
      kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks);
  if (query == nullptr) {
    throw std::runtime_error("Failed to allocate keychain query");
  }

  ScopedCFTypeRef serviceRef = makeCFString(service);
  ScopedCFTypeRef accountRef = makeCFString(account);
  if (serviceRef.get() == nullptr || accountRef.get() == nullptr) {
    CFRelease(query);
    throw std::runtime_error("Failed to build keychain query strings");
  }

  CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
  CFDictionarySetValue(query, kSecAttrService, serviceRef.get());
  CFDictionarySetValue(query, kSecAttrAccount, accountRef.get());
  return query;
}

} // namespace

void MacosSecureStorage::storeSecret(const std::string &service,
                                     const std::string &account,
                                     const std::vector<uint8_t> &secret) {
  validateInputs(service, account, &secret);

  ScopedCFTypeRef secretData = makeCFData(secret);
  if (secretData.get() == nullptr) {
    throw std::runtime_error("Failed to build keychain secret data");
  }

  ScopedCFTypeRef addQuery(createBaseQuery(service, account));
  CFDictionarySetValue(asMutableDictionary(addQuery), kSecValueData,
                       secretData.get());

  const OSStatus addStatus =
      SecItemAdd(static_cast<CFDictionaryRef>(addQuery.get()), nullptr);
  if (addStatus == errSecDuplicateItem) {
    ScopedCFTypeRef matchQuery(createBaseQuery(service, account));
    CFMutableDictionaryRef updateAttrs = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    if (updateAttrs == nullptr) {
      throw std::runtime_error("Failed to allocate keychain update attributes");
    }
    ScopedCFTypeRef updateAttrsRef(updateAttrs);
    CFDictionarySetValue(updateAttrs, kSecValueData, secretData.get());

    const OSStatus updateStatus =
        SecItemUpdate(static_cast<CFDictionaryRef>(matchQuery.get()),
                      static_cast<CFDictionaryRef>(updateAttrs));
    if (updateStatus != errSecSuccess) {
      throw makeKeychainError("Failed to update keychain item", updateStatus);
    }
    return;
  }

  if (addStatus != errSecSuccess) {
    throw makeKeychainError("Failed to store secret", addStatus);
  }
}

std::optional<std::vector<uint8_t>>
MacosSecureStorage::loadSecret(const std::string &service,
                               const std::string &account) {
  validateInputs(service, account);

  ScopedCFTypeRef query(createBaseQuery(service, account));
  CFDictionarySetValue(asMutableDictionary(query), kSecReturnData,
                       kCFBooleanTrue);
  CFDictionarySetValue(asMutableDictionary(query), kSecMatchLimit,
                       kSecMatchLimitOne);

  CFTypeRef result = nullptr;
  const OSStatus status =
      SecItemCopyMatching(static_cast<CFDictionaryRef>(query.get()), &result);

  if (status == errSecItemNotFound) {
    return std::nullopt;
  }
  if (status != errSecSuccess) {
    throw makeKeychainError("Failed to load secret", status);
  }

  ScopedCFTypeRef resultRef(result);
  if (CFGetTypeID(result) != CFDataGetTypeID()) {
    throw std::runtime_error("Loaded keychain secret has unexpected type");
  }

  CFDataRef data = static_cast<CFDataRef>(result);
  const UInt8 *bytes = CFDataGetBytePtr(data);
  const CFIndex length = CFDataGetLength(data);
  std::vector<uint8_t> secret(bytes, bytes + length);
  return secret;
}

void MacosSecureStorage::deleteSecret(const std::string &service,
                                      const std::string &account) {
  validateInputs(service, account);

  ScopedCFTypeRef query(createBaseQuery(service, account));
  const OSStatus status =
      SecItemDelete(static_cast<CFDictionaryRef>(query.get()));
  if (status == errSecItemNotFound) {
    return;
  }
  if (status != errSecSuccess) {
    throw makeKeychainError("Failed to delete secret", status);
  }
}
