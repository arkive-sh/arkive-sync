#include "platform/linux/SecureStorage.hpp"

#include <gio/gio.h>
#include <libsecret/secret.h>
#include <stdexcept>

namespace {

constexpr SecretSchema kArkiveSyncSecretSchema = {
    "arkive.sh.secure-storage",
    SECRET_SCHEMA_NONE,
    {
        {"service", SECRET_SCHEMA_ATTRIBUTE_STRING},
        {"account", SECRET_SCHEMA_ATTRIBUTE_STRING},
        {nullptr, static_cast<SecretSchemaAttributeType>(0)},
    },
};

std::string encodeBase64(const std::vector<uint8_t> &secret) {
  gchar *encoded = g_base64_encode(secret.data(), secret.size());
  if (encoded == nullptr) {
    throw std::runtime_error("Failed to base64-encode secret for storage");
  }

  std::string value(encoded);
  g_free(encoded);
  return value;
}

std::vector<uint8_t> decodeBase64(const std::string &secret) {
  gsize decodedLength = 0;
  guchar *decoded = g_base64_decode(secret.c_str(), &decodedLength);
  if (decoded == nullptr) {
    throw std::runtime_error("Failed to decode stored secret");
  }

  std::vector<uint8_t> bytes(decoded, decoded + decodedLength);
  g_free(decoded);
  return bytes;
}

std::runtime_error makeSecretError(const std::string &prefix, GError *error) {
  const std::string message =
      prefix + ": " + (error != nullptr ? error->message : "unknown error");
  if (error != nullptr) {
    g_error_free(error);
  }
  return std::runtime_error(message);
}

} // namespace

void LinuxSecureStorage::storeSecret(const std::string &service,
                                     const std::string &account,
                                     const std::vector<uint8_t> &secret) {
  if (service.empty() || account.empty()) {
    throw std::invalid_argument("SecureStorage service/account cannot be empty");
  }
  if (secret.empty()) {
    throw std::invalid_argument("SecureStorage secret cannot be empty");
  }

  GError *error = nullptr;
  const std::string encoded = encodeBase64(secret);
  const std::string label = service + " " + account;

  const gboolean ok = secret_password_store_sync(
      &kArkiveSyncSecretSchema, SECRET_COLLECTION_DEFAULT, label.c_str(),
      encoded.c_str(), nullptr, &error, "service", service.c_str(), "account",
      account.c_str(), nullptr);
  if (!ok) {
    throw makeSecretError("Failed to store secret", error);
  }
}

std::optional<std::vector<uint8_t>>
LinuxSecureStorage::loadSecret(const std::string &service,
                               const std::string &account) {
  if (service.empty() || account.empty()) {
    throw std::invalid_argument("SecureStorage service/account cannot be empty");
  }

  GError *error = nullptr;
  gchar *secret = secret_password_lookup_sync(
      &kArkiveSyncSecretSchema, nullptr, &error, "service", service.c_str(),
      "account", account.c_str(), nullptr);
  if (error != nullptr) {
    throw makeSecretError("Failed to load secret", error);
  }
  if (secret == nullptr) {
    return std::nullopt;
  }

  const std::string encoded(secret);
  secret_password_free(secret);
  return decodeBase64(encoded);
}

void LinuxSecureStorage::deleteSecret(const std::string &service,
                                      const std::string &account) {
  if (service.empty() || account.empty()) {
    throw std::invalid_argument("SecureStorage service/account cannot be empty");
  }

  GError *error = nullptr;
  const gboolean ok = secret_password_clear_sync(
      &kArkiveSyncSecretSchema, nullptr, &error, "service", service.c_str(),
      "account", account.c_str(), nullptr);
  if (error != nullptr) {
    throw makeSecretError("Failed to delete secret", error);
  }
  (void)ok;
}
