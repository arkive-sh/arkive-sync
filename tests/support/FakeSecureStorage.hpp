#pragma once

#include "platform/SecureStorage.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class FakeSecureStorage : public SecureStorage {
public:
  FakeSecureStorage() = default;

  FakeSecureStorage(const FakeSecureStorage &) = delete;
  FakeSecureStorage &operator=(const FakeSecureStorage &) = delete;

  FakeSecureStorage(FakeSecureStorage &&) = delete;
  FakeSecureStorage &operator=(FakeSecureStorage &&) = delete;

  void storeSecret(const std::string &service, const std::string &account,
                   const std::vector<uint8_t> &secret) override {
    secrets_[service + "\n" + account] = secret;
  }

  std::optional<std::vector<uint8_t>>
  loadSecret(const std::string &service,
             const std::string &account) override {
    const auto it = secrets_.find(service + "\n" + account);
    if (it == secrets_.end()) {
      return std::nullopt;
    }
    return it->second;
  }

  void deleteSecret(const std::string &service,
                    const std::string &account) override {
    secrets_.erase(service + "\n" + account);
  }

  const std::unordered_map<std::string, std::vector<uint8_t>> &secrets() const {
    return secrets_;
  }

private:
  std::unordered_map<std::string, std::vector<uint8_t>> secrets_;
};
