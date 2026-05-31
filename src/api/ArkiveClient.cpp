#include "./ArkiveClient.hpp"
#include <curl/curl.h>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <utility>

namespace {

using CurlPtr = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;
using HeaderPtr = std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)>;

CurlPtr makeCurlHandle() {
  CurlPtr curl(curl_easy_init(), &curl_easy_cleanup);
  if (!curl) {
    throw std::runtime_error("failed to initialize curl");
  }

  return curl;
}

void appendHeader(HeaderPtr &headers, const char *header) {
  curl_slist *raw_headers = curl_slist_append(headers.get(), header);
  if (raw_headers == nullptr) {
    throw std::runtime_error("failed to append curl header");
  }

  curl_slist *release = headers.release();
  headers.reset(raw_headers);
}

} // namespace

ArkiveClient::ArkiveClient(std::string baseUrl, std::string cookiePath)
    : baseUrl_(std::move(baseUrl)), cookiePath_(std::move(cookiePath)) {
  const std::filesystem::path cookie_file_path(cookiePath_);
  if (cookie_file_path.has_parent_path()) {
    std::filesystem::create_directories(cookie_file_path.parent_path());
  }
}

ArkiveClient::ArkiveClient(ArkiveClient &&other) noexcept
    : baseUrl_(std::move(other.baseUrl_)),
      cookiePath_(std::move(other.cookiePath_)) {}

ArkiveClient &ArkiveClient::operator=(ArkiveClient &&other) noexcept {
  if (this != &other) {
    baseUrl_ = std::move(other.baseUrl_);
    cookiePath_ = std::move(other.cookiePath_);
  }

  return *this;
}

static size_t writeCallback(char *ptr, size_t size, size_t nmemb,
                            void *userdata) {
  auto *output = static_cast<std::string *>(userdata);
  output->append(ptr, size * nmemb);
  return size * nmemb;
}

std::string ArkiveClient::url(const std::string &path) const {
  if (!path.empty() && path[0] == '/') {
    return baseUrl_ + path;
  }

  return baseUrl_ + "/" + path;
}

nlohmann::json ArkiveClient::postJson(const std::string &path,
                                      const nlohmann::json &body) {
  CurlPtr curl = makeCurlHandle();

  std::string response;
  std::string requestUrl = url(path);
  std::string bodyText = body.dump();

  HeaderPtr headers(nullptr, &curl_slist_free_all);
  appendHeader(headers, "Content-Type: application/json");
  appendHeader(headers, "Accept: application/json");

  curl_easy_setopt(curl.get(), CURLOPT_URL, requestUrl.c_str());
  curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
  curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
  curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, bodyText.c_str());
  curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, bodyText.size());
  curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, writeCallback);
  curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);

  curl_easy_setopt(curl.get(), CURLOPT_COOKIEFILE, cookiePath_.c_str());
  curl_easy_setopt(curl.get(), CURLOPT_COOKIEJAR, cookiePath_.c_str());

  CURLcode code = curl_easy_perform(curl.get());

  long status = 0;
  curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);

  if (code != CURLE_OK) {
    throw std::runtime_error(curl_easy_strerror(code));
  }

  if (status < 200 || status >= 300) {
    throw std::runtime_error("HTTP " + std::to_string(status) + ": " +
                             response);
  }

  if (response.empty()) {
    return nlohmann::json::object();
  }

  return nlohmann::json::parse(response);
}

nlohmann::json ArkiveClient::getJson(const std::string &path) {
  CurlPtr curl = makeCurlHandle();

  std::string response;
  std::string requestUrl = url(path);

  HeaderPtr headers(nullptr, &curl_slist_free_all);
  appendHeader(headers, "Accept: application/json");

  curl_easy_setopt(curl.get(), CURLOPT_URL, requestUrl.c_str());
  curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
  curl_easy_setopt(curl.get(), CURLOPT_HTTPGET, 1L);
  curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, writeCallback);
  curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);

  curl_easy_setopt(curl.get(), CURLOPT_COOKIEFILE, cookiePath_.c_str());
  curl_easy_setopt(curl.get(), CURLOPT_COOKIEJAR, cookiePath_.c_str());
  curl_easy_setopt(curl.get(), CURLOPT_COOKIELIST, "FLUSH");

  CURLcode code = curl_easy_perform(curl.get());

  long status = 0;
  curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);

  if (code != CURLE_OK) {
    throw std::runtime_error(curl_easy_strerror(code));
  }

  if (status < 200 || status >= 300) {
    throw std::runtime_error("HTTP " + std::to_string(status) + ": " +
                             response);
  }

  if (response.empty()) {
    return nlohmann::json::object();
  }

  return nlohmann::json::parse(response);
}

void ArkiveClient::postForm(const std::string &path) {
  CurlPtr curl = makeCurlHandle();

  std::string response;
  std::string requestUrl = url(path);

  curl_easy_setopt(curl.get(), CURLOPT_URL, requestUrl.c_str());
  curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
  curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, "");
  curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, 0L);
  curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, writeCallback);
  curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);

  curl_easy_setopt(curl.get(), CURLOPT_COOKIEFILE, cookiePath_.c_str());
  curl_easy_setopt(curl.get(), CURLOPT_COOKIEJAR, cookiePath_.c_str());

  CURLcode code = curl_easy_perform(curl.get());

  long status = 0;
  curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);

  if (code != CURLE_OK) {
    throw std::runtime_error(curl_easy_strerror(code));
  }

  if (status < 200 || status >= 400) {
    throw std::runtime_error("HTTP " + std::to_string(status) + ": " +
                             response);
  }
}

LoginResponse ArkiveClient::login(const std::string &email,
                                  const std::string &password) {
  auto res =
      postJson("/api/auth/login", {{"email", email}, {"password", password}});

  return LoginResponse{
      .salt = res.value("salt", ""),
      .encryptedMasterKey = res.value("encryptedMasterKey", ""),
  };
}

void ArkiveClient::logout() { postForm("/logout"); }

nlohmann::json ArkiveClient::me() { return getJson("/api/me"); }
