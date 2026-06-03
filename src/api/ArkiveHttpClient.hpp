#pragma once

#include "api/HttpError.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

class ArkiveHttpClient {
public:
  explicit ArkiveHttpClient(std::string baseUrl, std::string cookiePath);

  // Allow move ownership
  ArkiveHttpClient(ArkiveHttpClient &&other) noexcept;
  ArkiveHttpClient &operator=(ArkiveHttpClient &&other) noexcept;

  nlohmann::json postJson(const std::string &path, const nlohmann::json &body);
  nlohmann::json getJson(const std::string &path);
  void postForm(const std::string &path);
  std::string putBytes(const std::string &pathOrUrl,
                       const std::vector<std::byte> &body);

private:
  std::string baseUrl_;
  std::string cookiePath_;

  std::string url(const std::string &path) const;
};
