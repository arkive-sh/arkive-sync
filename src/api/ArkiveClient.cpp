#include "./ArkiveClient.hpp"
#include <curl/curl.h>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <utility>

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
  CURL *curl = curl_easy_init();
  if (!curl) {
    throw std::runtime_error("failed to initialize curl");
  }

  std::string response;
  std::string requestUrl = url(path);
  std::string bodyText = body.dump();

  struct curl_slist *headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  headers = curl_slist_append(headers, "Accept: application/json");

  curl_easy_setopt(curl, CURLOPT_URL, requestUrl.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, bodyText.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, bodyText.size());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

  curl_easy_setopt(curl, CURLOPT_COOKIEFILE, cookiePath_.c_str());
  curl_easy_setopt(curl, CURLOPT_COOKIEJAR, cookiePath_.c_str());

  CURLcode code = curl_easy_perform(curl);

  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

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
  CURL *curl = curl_easy_init();
  if (!curl) {
    throw std::runtime_error("failed to initialize curl");
  }

  std::string response;
  std::string requestUrl = url(path);

  struct curl_slist *headers = nullptr;
  headers = curl_slist_append(headers, "Accept: application/json");

  curl_easy_setopt(curl, CURLOPT_URL, requestUrl.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

  curl_easy_setopt(curl, CURLOPT_COOKIEFILE, cookiePath_.c_str());
  curl_easy_setopt(curl, CURLOPT_COOKIEJAR, cookiePath_.c_str());
  curl_easy_setopt(curl, CURLOPT_COOKIELIST, "FLUSH");

  CURLcode code = curl_easy_perform(curl);

  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

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

LoginResponse ArkiveClient::login(const std::string &email,
                                  const std::string &password) {
  auto res =
      postJson("/api/auth/login", {{"email", email}, {"password", password}});

  return LoginResponse{
      .salt = res.value("salt", ""),
      .encryptedMasterKey = res.value("encryptedMasterKey", ""),
  };
}

nlohmann::json ArkiveClient::me() { return getJson("/api/me"); }
