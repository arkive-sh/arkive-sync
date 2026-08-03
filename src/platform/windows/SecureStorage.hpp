#pragma once

#include "platform/SecureStorage.hpp"

class WindowsSecureStorage final : public SecureStorage {
public:
  void storeSecret(const std::string &service, const std::string &account,
                   const std::vector<uint8_t> &secret) override;

  std::optional<std::vector<uint8_t>>
  loadSecret(const std::string &service, const std::string &account) override;

  void deleteSecret(const std::string &service,
                    const std::string &account) override;
};
