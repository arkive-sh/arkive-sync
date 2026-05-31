#pragma once

#include <nlohmann/json.hpp>
#include <string>

struct LoginResponse {
  std::string salt;
  std::string encryptedMasterKey;
};

class ArkiveClient {
public:
  explicit ArkiveClient(std::string baseUrl, std::string cookiePath);

  // Allow move ownership
  ArkiveClient(ArkiveClient &&other) noexcept;
  ArkiveClient &operator=(ArkiveClient &&other) noexcept;

  LoginResponse login(const std::string &email, const std::string &password);
  nlohmann::json me();

private:
  std::string baseUrl_;
  std::string cookiePath_;

  nlohmann::json postJson(const std::string &path, const nlohmann::json &body);
  nlohmann::json getJson(const std::string &path);

  std::string url(const std::string &path) const;
};
