#include "platform/SecureStorage.hpp"

#include <exception>
#include <iostream>
#include <string>
#include <vector>

int main() {
  const std::string service = "arkive-sync-ci";
  const std::string account = "secure-storage-smoke";
  const std::vector<uint8_t> secret = {0x61, 0x72, 0x6b, 0x69,
                                       0x76, 0x65, 0x01, 0xff};

  try {
    auto storage = SecureStorage::create();
    storage->deleteSecret(service, account);
    storage->storeSecret(service, account, secret);

    const auto loaded = storage->loadSecret(service, account);
    if (!loaded.has_value() || *loaded != secret) {
      std::cerr << "secure storage smoke failed: loaded secret mismatch\n";
      return 1;
    }

    storage->deleteSecret(service, account);
    if (storage->loadSecret(service, account).has_value()) {
      std::cerr << "secure storage smoke failed: secret still present\n";
      return 1;
    }
  } catch (const std::exception &error) {
    std::cerr << "secure storage smoke failed: " << error.what() << "\n";
    return 1;
  }

  return 0;
}
