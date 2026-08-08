#pragma once

#include "networking/curl.hpp"
#include <cstdint>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

class ArkiveHttpClient {
public:
  using ByteSink =
      std::function<void(const uint8_t *data, std::size_t size)>;

  explicit ArkiveHttpClient(std::string baseUrl, std::string cookiePath);
  virtual ~ArkiveHttpClient();

  // Allow move ownership
  ArkiveHttpClient(ArkiveHttpClient &&other) noexcept;
  ArkiveHttpClient &operator=(ArkiveHttpClient &&other) noexcept;

  virtual nlohmann::json postJson(const std::string &path,
                                  const nlohmann::json &body);
  virtual nlohmann::json getJson(const std::string &path);
  virtual void postForm(const std::string &path);
  virtual std::string putBytes(const std::string &pathOrUrl,
                               const std::vector<uint8_t> &body);
  virtual void getRangeToSink(const std::string &pathOrUrl, uint64_t offset,
                              uint64_t length, const ByteSink &sink);

private:
  std::string baseUrl_;
  networking::CurlService curl_;

  std::string url(const std::string &path) const;
};
