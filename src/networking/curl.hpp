#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace networking {

class CurlService {
public:
  using ByteSink =
      std::function<void(const uint8_t *data, std::size_t size)>;

  explicit CurlService(std::string cookiePath);
  ~CurlService();

  CurlService(CurlService &&other) noexcept;
  CurlService &operator=(CurlService &&other) noexcept;

  nlohmann::json postJson(const std::string &requestUrl,
                          const nlohmann::json &body);
  nlohmann::json getJson(const std::string &requestUrl);
  void postForm(const std::string &requestUrl);
  std::string putBytes(const std::string &requestUrl,
                       const std::vector<uint8_t> &body);
  void getRangeToSink(const std::string &requestUrl, uint64_t offset,
                      uint64_t length, const ByteSink &sink);

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace networking
