#include "networking/curl.hpp"

#include "api/HttpError.hpp"
#include "networking/StoragePutTransport.hpp"
#include <cctype>
#include <curl/curl.h>
#include <filesystem>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {

void lockCookieShare(CURL *, curl_lock_data, curl_lock_access, void *userData) {
  static_cast<std::mutex *>(userData)->lock();
}

void unlockCookieShare(CURL *, curl_lock_data, void *userData) {
  static_cast<std::mutex *>(userData)->unlock();
}

using CurlPtr = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;
using HeaderPtr = std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)>;

struct SinkContext {
  const networking::CurlService::ByteSink *sink;
};

size_t writeCallback(char *ptr, size_t size, size_t nmemb,
                     void *userdata) {
  auto *output = static_cast<std::string *>(userdata);
  output->append(ptr, size * nmemb);
  return size * nmemb;
}

size_t sinkCallback(char *ptr, size_t size, size_t nmemb, void *userdata) {
  auto *ctx = static_cast<SinkContext *>(userdata);
  const size_t total = size * nmemb;
  (*ctx->sink)(reinterpret_cast<const uint8_t *>(ptr), total);
  return total;
}

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

class networking::CurlService::Impl {
public:
  explicit Impl(std::string cookiePath) : cookiePath_(std::move(cookiePath)) {
    const std::filesystem::path cookieFilePath(cookiePath_);
    if (cookieFilePath.has_parent_path()) {
      std::filesystem::create_directories(cookieFilePath.parent_path());
    }

    cookieShare_ = curl_share_init();
    if (cookieShare_ == nullptr ||
        curl_share_setopt(cookieShare_, CURLSHOPT_SHARE,
                          CURL_LOCK_DATA_COOKIE) != CURLSHE_OK ||
        curl_share_setopt(cookieShare_, CURLSHOPT_LOCKFUNC, lockCookieShare) !=
            CURLSHE_OK ||
        curl_share_setopt(cookieShare_, CURLSHOPT_UNLOCKFUNC,
                          unlockCookieShare) != CURLSHE_OK ||
        curl_share_setopt(cookieShare_, CURLSHOPT_USERDATA, &cookieMutex_) !=
            CURLSHE_OK) {
      if (cookieShare_ != nullptr) {
        curl_share_cleanup(cookieShare_);
        cookieShare_ = nullptr;
      }
      throw std::runtime_error("failed to initialize curl cookie sharing");
    }

    try {
      storagePutTransport_ =
          std::make_unique<networking::StoragePutTransport>();
    } catch (...) {
      curl_share_cleanup(cookieShare_);
      cookieShare_ = nullptr;
      throw;
    }
  }

  ~Impl() {
    storagePutTransport_.reset();
    if (cookieShare_ != nullptr) {
      curl_share_cleanup(cookieShare_);
    }
  }

  Impl(Impl &&other) noexcept
      : cookiePath_(std::move(other.cookiePath_)),
        cookieShare_(other.cookieShare_),
        storagePutTransport_(std::move(other.storagePutTransport_)) {
    if (cookieShare_ != nullptr) {
      curl_share_setopt(cookieShare_, CURLSHOPT_USERDATA, &cookieMutex_);
    }
    other.cookieShare_ = nullptr;
  }

  Impl &operator=(Impl &&other) noexcept {
    if (this != &other) {
      cookiePath_ = std::move(other.cookiePath_);
      if (cookieShare_ != nullptr) {
        curl_share_cleanup(cookieShare_);
      }
      cookieShare_ = other.cookieShare_;
      storagePutTransport_ = std::move(other.storagePutTransport_);
      if (cookieShare_ != nullptr) {
        curl_share_setopt(cookieShare_, CURLSHOPT_USERDATA, &cookieMutex_);
      }
      other.cookieShare_ = nullptr;
    }
    return *this;
  }

  void configure(CURL *curl) const {
    curl_easy_setopt(curl, CURLOPT_SHARE, cookieShare_);
    curl_easy_setopt(curl, CURLOPT_COOKIEFILE, cookiePath_.c_str());
    curl_easy_setopt(curl, CURLOPT_COOKIEJAR, cookiePath_.c_str());
  }

  std::string cookiePath_;
  CURLSH *cookieShare_{nullptr};
  mutable std::mutex cookieMutex_;
  std::unique_ptr<networking::StoragePutTransport> storagePutTransport_;
};

namespace networking {

CurlService::CurlService(std::string cookiePath)
    : impl_(std::make_unique<Impl>(std::move(cookiePath))) {}

CurlService::~CurlService() = default;

CurlService::CurlService(CurlService &&other) noexcept
    : impl_(std::move(other.impl_)) {}

CurlService &CurlService::operator=(CurlService &&other) noexcept {
  impl_ = std::move(other.impl_);
  return *this;
}

nlohmann::json CurlService::postJson(const std::string &requestUrl,
                                     const nlohmann::json &body) {
  CurlPtr curl = makeCurlHandle();
  impl_->configure(curl.get());
  std::string response;
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
  const CURLcode code = curl_easy_perform(curl.get());
  curl_easy_setopt(curl.get(), CURLOPT_COOKIELIST, "FLUSH");
  long status = 0;
  curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
  if (code != CURLE_OK) {
    throw std::runtime_error(curl_easy_strerror(code));
  }
  if (status < 200 || status >= 300) {
    throw HttpError(static_cast<int>(status), response);
  }
  return response.empty() ? nlohmann::json::object()
                          : nlohmann::json::parse(response);
}

nlohmann::json CurlService::getJson(const std::string &requestUrl) {
  CurlPtr curl = makeCurlHandle();
  impl_->configure(curl.get());
  std::string response;
  HeaderPtr headers(nullptr, &curl_slist_free_all);
  appendHeader(headers, "Accept: application/json");
  curl_easy_setopt(curl.get(), CURLOPT_URL, requestUrl.c_str());
  curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
  curl_easy_setopt(curl.get(), CURLOPT_HTTPGET, 1L);
  curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, writeCallback);
  curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl.get(), CURLOPT_COOKIELIST, "FLUSH");
  const CURLcode code = curl_easy_perform(curl.get());
  long status = 0;
  curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
  if (code != CURLE_OK) {
    throw std::runtime_error(curl_easy_strerror(code));
  }
  if (status < 200 || status >= 300) {
    throw HttpError(static_cast<int>(status), response);
  }
  return response.empty() ? nlohmann::json::object()
                          : nlohmann::json::parse(response);
}

void CurlService::postForm(const std::string &requestUrl) {
  CurlPtr curl = makeCurlHandle();
  impl_->configure(curl.get());
  std::string response;
  curl_easy_setopt(curl.get(), CURLOPT_URL, requestUrl.c_str());
  curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
  curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, "");
  curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, 0L);
  curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, writeCallback);
  curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);
  const CURLcode code = curl_easy_perform(curl.get());
  long status = 0;
  curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
  if (code != CURLE_OK) {
    throw std::runtime_error(curl_easy_strerror(code));
  }
  if (status < 200 || status >= 400) {
    throw HttpError(static_cast<int>(status), response);
  }
}

std::string CurlService::putBytes(const std::string &requestUrl,
                                  const std::vector<uint8_t> &body) {
  return impl_->storagePutTransport_->put(requestUrl, body);
}

void CurlService::getRangeToSink(const std::string &requestUrl, uint64_t offset,
                                 uint64_t length, const ByteSink &sink) {
  if (length == 0) {
    return;
  }

  CurlPtr curl = makeCurlHandle();
  impl_->configure(curl.get());
  std::string response;
  const std::string range =
      std::to_string(offset) + "-" + std::to_string(offset + length - 1);
  SinkContext ctx{&sink};
  curl_easy_setopt(curl.get(), CURLOPT_URL, requestUrl.c_str());
  curl_easy_setopt(curl.get(), CURLOPT_HTTPGET, 1L);
  curl_easy_setopt(curl.get(), CURLOPT_RANGE, range.c_str());
  curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, sinkCallback);
  curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &ctx);
  const CURLcode code = curl_easy_perform(curl.get());
  long status = 0;
  curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
  if (code != CURLE_OK) {
    throw std::runtime_error(curl_easy_strerror(code));
  }
  if (status != 206) {
    throw HttpError(static_cast<int>(status), response);
  }
}

} // namespace networking
