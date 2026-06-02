#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class SecureStorage {
public:
  virtual ~SecureStorage() = default;

  virtual void storeSecret(const std::string &service,
                           const std::string &account,
                           const std::vector<uint8_t> &secret) = 0;

  virtual std::optional<std::vector<uint8_t>>
  loadSecret(const std::string &service, const std::string &account) = 0;

  virtual void deleteSecret(const std::string &service,
                            const std::string &account) = 0;

  static std::unique_ptr<SecureStorage> create();
};
