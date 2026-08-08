#include "api/ArkiveHttpClient.hpp"

#include <utility>

ArkiveHttpClient::ArkiveHttpClient(std::string baseUrl, std::string cookiePath)
    : baseUrl_(std::move(baseUrl)), curl_(std::move(cookiePath)) {}

ArkiveHttpClient::~ArkiveHttpClient() = default;

ArkiveHttpClient::ArkiveHttpClient(ArkiveHttpClient &&other) noexcept
    : baseUrl_(std::move(other.baseUrl_)), curl_(std::move(other.curl_)) {}

ArkiveHttpClient &ArkiveHttpClient::operator=(ArkiveHttpClient &&other) noexcept {
  if (this != &other) {
    baseUrl_ = std::move(other.baseUrl_);
    curl_ = std::move(other.curl_);
  }
  return *this;
}

std::string ArkiveHttpClient::url(const std::string &path) const {
  if (path.rfind("http://", 0) == 0 || path.rfind("https://", 0) == 0) {
    return path;
  }
  if (!path.empty() && path[0] == '/') {
    return baseUrl_ + path;
  }
  return baseUrl_ + "/" + path;
}

nlohmann::json ArkiveHttpClient::postJson(const std::string &path,
                                          const nlohmann::json &body) {
  return curl_.postJson(url(path), body);
}

nlohmann::json ArkiveHttpClient::getJson(const std::string &path) {
  return curl_.getJson(url(path));
}

void ArkiveHttpClient::postForm(const std::string &path) {
  curl_.postForm(url(path));
}

std::string ArkiveHttpClient::putBytes(const std::string &pathOrUrl,
                                       const std::vector<uint8_t> &body) {
  return curl_.putBytes(url(pathOrUrl), body);
}

void ArkiveHttpClient::getRangeToSink(const std::string &pathOrUrl,
                                      uint64_t offset, uint64_t length,
                                      const ByteSink &sink) {
  curl_.getRangeToSink(url(pathOrUrl), offset, length, sink);
}
