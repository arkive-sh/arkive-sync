#include "./ArkiveHttpClient.hpp"
#include <cctype>
#include <curl/curl.h>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string_view>
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

ArkiveHttpClient::ArkiveHttpClient(std::string baseUrl, std::string cookiePath)
    : baseUrl_(std::move(baseUrl)), cookiePath_(std::move(cookiePath)) {
  const std::filesystem::path cookie_file_path(cookiePath_);
  if (cookie_file_path.has_parent_path()) {
    std::filesystem::create_directories(cookie_file_path.parent_path());
  }
}

ArkiveHttpClient::ArkiveHttpClient(ArkiveHttpClient &&other) noexcept
    : baseUrl_(std::move(other.baseUrl_)),
      cookiePath_(std::move(other.cookiePath_)) {}

ArkiveHttpClient &ArkiveHttpClient::operator=(ArkiveHttpClient &&other) noexcept {
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

static size_t headerCallback(char *buffer, size_t size, size_t nitems,
                             void *userdata) {
  const size_t total = size * nitems;
  auto *etag = static_cast<std::string *>(userdata);
  const std::string_view header(buffer, total);
  static constexpr std::string_view kEtagPrefix = "etag:";

  if (header.size() >= kEtagPrefix.size()) {
    std::string normalized(header.substr(0, kEtagPrefix.size()));
    for (char &ch : normalized) {
      ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }

    if (normalized == kEtagPrefix) {
      std::string value(header.substr(kEtagPrefix.size()));
      const size_t first = value.find_first_not_of(" \t");
      if (first != std::string::npos) {
        value.erase(0, first);
      } else {
        value.clear();
      }

      while (!value.empty() &&
             (value.back() == '\r' || value.back() == '\n' ||
              value.back() == ' ' || value.back() == '\t')) {
        value.pop_back();
      }

      *etag = std::move(value);
    }
  }

  return total;
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
    throw HttpError(static_cast<int>(status), response);
  }

  if (response.empty()) {
    return nlohmann::json::object();
  }

  return nlohmann::json::parse(response);
}

nlohmann::json ArkiveHttpClient::getJson(const std::string &path) {
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
    throw HttpError(static_cast<int>(status), response);
  }

  if (response.empty()) {
    return nlohmann::json::object();
  }

  return nlohmann::json::parse(response);
}

void ArkiveHttpClient::postForm(const std::string &path) {
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
    throw HttpError(status, response);
  }
}

std::string ArkiveHttpClient::putBytes(const std::string &pathOrUrl,
                                       const std::vector<std::byte> &body) {
  CurlPtr curl = makeCurlHandle();
  HeaderPtr headers(nullptr, &curl_slist_free_all);

  std::string response;
  std::string etag;
  const std::string requestUrl = url(pathOrUrl);

  appendHeader(headers, "Content-Type: application/octet-stream");

  curl_easy_setopt(curl.get(), CURLOPT_URL, requestUrl.c_str());
  curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
  curl_easy_setopt(curl.get(), CURLOPT_CUSTOMREQUEST, "PUT");
  curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS,
                   reinterpret_cast<const char *>(body.data()));
  curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, body.size());
  curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, writeCallback);
  curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl.get(), CURLOPT_HEADERFUNCTION, headerCallback);
  curl_easy_setopt(curl.get(), CURLOPT_HEADERDATA, &etag);

  if (requestUrl.rfind(baseUrl_, 0) == 0) {
    curl_easy_setopt(curl.get(), CURLOPT_COOKIEFILE, cookiePath_.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_COOKIEJAR, cookiePath_.c_str());
  }

  CURLcode code = curl_easy_perform(curl.get());

  long status = 0;
  curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);

  if (code != CURLE_OK) {
    throw std::runtime_error(curl_easy_strerror(code));
  }

  if (status < 200 || status >= 300) {
    throw HttpError(status, response);
  }

  return etag;
}
