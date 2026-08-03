#include "platform/windows/SecureStorage.hpp"

#include <windows.h>
#include <wincred.h>

#include <memory>
#include <stdexcept>
#include <string>

namespace {

void validateInputs(const std::string &service, const std::string &account,
                    const std::vector<uint8_t> *secret = nullptr) {
  if (service.empty() || account.empty()) {
    throw std::invalid_argument("SecureStorage service/account cannot be empty");
  }
  if (secret != nullptr && secret->empty()) {
    throw std::invalid_argument("SecureStorage secret cannot be empty");
  }
  if (secret != nullptr && secret->size() > CRED_MAX_CREDENTIAL_BLOB_SIZE) {
    throw std::invalid_argument("SecureStorage secret exceeds Windows "
                                "Credential Manager blob size limit");
  }
}

std::wstring utf8ToWide(const std::string &value) {
  if (value.empty()) {
    return {};
  }

  const int length = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
      static_cast<int>(value.size()), nullptr, 0);
  if (length == 0) {
    throw std::runtime_error("Failed to convert UTF-8 text to UTF-16");
  }

  std::wstring wide(static_cast<std::size_t>(length), L'\0');
  const int written = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
      static_cast<int>(value.size()), wide.data(), length);
  if (written == 0) {
    throw std::runtime_error("Failed to convert UTF-8 text to UTF-16");
  }

  return wide;
}

std::string windowsErrorMessage(DWORD error) {
  LPWSTR buffer = nullptr;
  const DWORD length = FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, error, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);

  if (length == 0 || buffer == nullptr) {
    return "Windows error " + std::to_string(error);
  }

  const std::wstring wide(buffer, buffer + length);
  LocalFree(buffer);

  const int utf8Length =
      WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                          static_cast<int>(wide.size()), nullptr, 0, nullptr,
                          nullptr);
  if (utf8Length == 0) {
    return "Windows error " + std::to_string(error);
  }

  std::string message(static_cast<std::size_t>(utf8Length), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                      message.data(), utf8Length, nullptr, nullptr);
  while (!message.empty() &&
         (message.back() == '\r' || message.back() == '\n' ||
          message.back() == ' ')) {
    message.pop_back();
  }
  return message;
}

std::runtime_error makeCredentialError(const std::string &prefix,
                                       DWORD error) {
  return std::runtime_error(prefix + ": " + windowsErrorMessage(error));
}

std::wstring makeTargetName(const std::string &service,
                            const std::string &account) {
  const std::string target = "arkive-sync:" + std::to_string(service.size()) +
                             ":" + service + ":" + account;
  return utf8ToWide(target);
}

struct CredentialDeleter {
  void operator()(PCREDENTIALW credential) const {
    if (credential != nullptr) {
      CredFree(credential);
    }
  }
};

} // namespace

void WindowsSecureStorage::storeSecret(const std::string &service,
                                       const std::string &account,
                                       const std::vector<uint8_t> &secret) {
  validateInputs(service, account, &secret);

  std::wstring targetName = makeTargetName(service, account);
  std::wstring userName = utf8ToWide(account);

  CREDENTIALW credential{};
  credential.Type = CRED_TYPE_GENERIC;
  credential.TargetName = targetName.data();
  credential.CredentialBlobSize = static_cast<DWORD>(secret.size());
  credential.CredentialBlob =
      const_cast<LPBYTE>(reinterpret_cast<const BYTE *>(secret.data()));
  credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
  credential.UserName = userName.data();

  if (!CredWriteW(&credential, 0)) {
    throw makeCredentialError("Failed to store credential", GetLastError());
  }
}

std::optional<std::vector<uint8_t>>
WindowsSecureStorage::loadSecret(const std::string &service,
                                 const std::string &account) {
  validateInputs(service, account);

  std::wstring targetName = makeTargetName(service, account);
  PCREDENTIALW rawCredential = nullptr;
  if (!CredReadW(targetName.c_str(), CRED_TYPE_GENERIC, 0, &rawCredential)) {
    const DWORD error = GetLastError();
    if (error == ERROR_NOT_FOUND) {
      return std::nullopt;
    }
    throw makeCredentialError("Failed to load credential", error);
  }

  std::unique_ptr<CREDENTIALW, CredentialDeleter> credential(rawCredential);
  if (credential->CredentialBlobSize == 0 ||
      credential->CredentialBlob == nullptr) {
    return std::vector<uint8_t>{};
  }

  const BYTE *begin = credential->CredentialBlob;
  const BYTE *end = begin + credential->CredentialBlobSize;
  return std::vector<uint8_t>(begin, end);
}

void WindowsSecureStorage::deleteSecret(const std::string &service,
                                        const std::string &account) {
  validateInputs(service, account);

  std::wstring targetName = makeTargetName(service, account);
  if (!CredDeleteW(targetName.c_str(), CRED_TYPE_GENERIC, 0)) {
    const DWORD error = GetLastError();
    if (error == ERROR_NOT_FOUND) {
      return;
    }
    throw makeCredentialError("Failed to delete credential", error);
  }
}
