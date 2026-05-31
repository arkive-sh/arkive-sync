#pragma once

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

class HttpError : public std::runtime_error {
public:
  HttpError(long statusCode, std::string responseBody);

  long statusCode() const noexcept;
  const std::string &responseBody() const noexcept;

private:
  long statusCode_;
  std::string responseBody_;
};

class ArkiveHttpClient {
public:
  explicit ArkiveHttpClient(std::string baseUrl, std::string cookiePath);

  // Allow move ownership
  ArkiveHttpClient(ArkiveHttpClient &&other) noexcept;
  ArkiveHttpClient &operator=(ArkiveHttpClient &&other) noexcept;

  nlohmann::json postJson(const std::string &path, const nlohmann::json &body);
  nlohmann::json getJson(const std::string &path);
  void postForm(const std::string &path);

private:
  std::string baseUrl_;
  std::string cookiePath_;

  std::string url(const std::string &path) const;
};
