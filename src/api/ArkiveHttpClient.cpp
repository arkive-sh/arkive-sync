#include "./ArkiveHttpClient.hpp"
#include <cctype>
#include <curl/curl.h>
#include <cstddef>
#include <condition_variable>
#include <filesystem>
#include <future>
#include <memory>
#include <queue>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <unordered_map>
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

struct StoragePutRequest {
  std::string url;
  std::vector<uint8_t> body;
  std::string response;
  std::string etag;
  HeaderPtr headers{nullptr, &curl_slist_free_all};
  std::promise<std::string> result;
  CURL *curl{nullptr};
};

size_t storageWriteCallback(char *ptr, size_t size, size_t nmemb,
                            void *userdata) {
  auto *request = static_cast<StoragePutRequest *>(userdata);
  request->response.append(ptr, size * nmemb);
  return size * nmemb;
}

size_t storageHeaderCallback(char *buffer, size_t size, size_t nitems,
                             void *userdata) {
  const size_t total = size * nitems;
  auto *request = static_cast<StoragePutRequest *>(userdata);
  const std::string_view header(buffer, total);
  static constexpr std::string_view kEtagPrefix = "etag:";
  if (header.size() < kEtagPrefix.size()) {
    return total;
  }

  std::string normalized(header.substr(0, kEtagPrefix.size()));
  for (char &ch : normalized) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  if (normalized != kEtagPrefix) {
    return total;
  }

  std::string value(header.substr(kEtagPrefix.size()));
  const size_t first = value.find_first_not_of(" \t");
  if (first == std::string::npos) {
    value.clear();
  } else {
    value.erase(0, first);
  }
  while (!value.empty() &&
         (value.back() == '\r' || value.back() == '\n' ||
          value.back() == ' ' || value.back() == '\t')) {
    value.pop_back();
  }
  request->etag = std::move(value);
  return total;
}

} // namespace

class ArkiveHttpClient::StoragePutTransport {
public:
  StoragePutTransport() : worker_([this] { run(); }) {}

  ~StoragePutTransport() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopping_ = true;
    }
    condition_.notify_one();
    worker_.join();
  }

  std::string put(std::string requestUrl, const std::vector<uint8_t> &body) {
    auto request = std::make_unique<StoragePutRequest>();
    request->url = std::move(requestUrl);
    request->body = body;
    std::future<std::string> result = request->result.get_future();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stopping_) {
        throw std::runtime_error("storage upload transport is stopped");
      }
      pending_.push(std::move(request));
    }
    condition_.notify_one();
    return result.get();
  }

private:
  void addPending() {
    std::queue<std::unique_ptr<StoragePutRequest>> pending;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      pending.swap(pending_);
    }

    while (!pending.empty()) {
      std::unique_ptr<StoragePutRequest> request = std::move(pending.front());
      pending.pop();
      request->curl = curl_easy_init();
      if (request->curl == nullptr) {
        request->result.set_exception(std::make_exception_ptr(
            std::runtime_error("failed to initialize curl storage handle")));
        continue;
      }

      request->headers.reset(curl_slist_append(
          nullptr, "Content-Type: application/octet-stream"));
      if (request->headers == nullptr) {
        curl_easy_cleanup(request->curl);
        request->curl = nullptr;
        request->result.set_exception(std::make_exception_ptr(
            std::runtime_error("failed to append curl storage header")));
        continue;
      }

      curl_easy_setopt(request->curl, CURLOPT_URL, request->url.c_str());
      curl_easy_setopt(request->curl, CURLOPT_HTTPHEADER,
                       request->headers.get());
      curl_easy_setopt(request->curl, CURLOPT_CUSTOMREQUEST, "PUT");
      curl_easy_setopt(request->curl, CURLOPT_POSTFIELDS,
                       reinterpret_cast<const char *>(request->body.data()));
      curl_easy_setopt(request->curl, CURLOPT_POSTFIELDSIZE,
                       request->body.size());
      curl_easy_setopt(request->curl, CURLOPT_WRITEFUNCTION,
                       storageWriteCallback);
      curl_easy_setopt(request->curl, CURLOPT_WRITEDATA, request.get());
      curl_easy_setopt(request->curl, CURLOPT_HEADERFUNCTION,
                       storageHeaderCallback);
      curl_easy_setopt(request->curl, CURLOPT_HEADERDATA, request.get());

      const CURLMcode code = curl_multi_add_handle(multi_, request->curl);
      if (code != CURLM_OK) {
        curl_easy_cleanup(request->curl);
        request->curl = nullptr;
        request->result.set_exception(std::make_exception_ptr(
            std::runtime_error(curl_multi_strerror(code))));
        continue;
      }
      active_.emplace(request->curl, std::move(request));
    }
  }

  void finish(std::unique_ptr<StoragePutRequest> request,
              std::exception_ptr error) {
    curl_multi_remove_handle(multi_, request->curl);
    curl_easy_cleanup(request->curl);
    request->curl = nullptr;
    if (error) {
      request->result.set_exception(std::move(error));
    } else {
      request->result.set_value(std::move(request->etag));
    }
  }

  void collectCompleted() {
    int messages = 0;
    while (CURLMsg *message = curl_multi_info_read(multi_, &messages)) {
      if (message->msg != CURLMSG_DONE) {
        continue;
      }
      auto it = active_.find(message->easy_handle);
      if (it == active_.end()) {
        continue;
      }

      std::unique_ptr<StoragePutRequest> request = std::move(it->second);
      active_.erase(it);
      long status = 0;
      curl_easy_getinfo(request->curl, CURLINFO_RESPONSE_CODE, &status);
      if (message->data.result != CURLE_OK) {
        finish(std::move(request), std::make_exception_ptr(std::runtime_error(
            curl_easy_strerror(message->data.result))));
      } else if (status < 200 || status >= 300) {
        finish(std::move(request), std::make_exception_ptr(
            HttpError(static_cast<int>(status), request->response)));
      } else {
        finish(std::move(request), nullptr);
      }
    }
  }

  void failAll(std::exception_ptr error) {
    while (!pending_.empty()) {
      pending_.front()->result.set_exception(error);
      pending_.pop();
    }
    for (auto &[handle, request] : active_) {
      curl_multi_remove_handle(multi_, handle);
      curl_easy_cleanup(handle);
      request->result.set_exception(error);
    }
    active_.clear();
  }

  void run() {
    multi_ = curl_multi_init();
    if (multi_ == nullptr) {
      std::lock_guard<std::mutex> lock(mutex_);
      stopping_ = true;
      failAll(std::make_exception_ptr(
          std::runtime_error("failed to initialize curl multi handle")));
      return;
    }

    int running = 0;
    while (true) {
      addPending();
      curl_multi_perform(multi_, &running);
      collectCompleted();

      std::unique_lock<std::mutex> lock(mutex_);
      if (stopping_ && pending_.empty() && active_.empty()) {
        break;
      }
      const bool hasPending = !pending_.empty();
      lock.unlock();

      if (running > 0) {
        int ready = 0;
        curl_multi_poll(multi_, nullptr, 0, 100, &ready);
      } else if (!hasPending) {
        lock.lock();
        condition_.wait(lock, [this] { return stopping_ || !pending_.empty(); });
      }
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      failAll(std::make_exception_ptr(
          std::runtime_error("storage upload transport stopped")));
    }
    curl_multi_cleanup(multi_);
    multi_ = nullptr;
  }

  std::mutex mutex_;
  std::condition_variable condition_;
  std::queue<std::unique_ptr<StoragePutRequest>> pending_;
  std::unordered_map<CURL *, std::unique_ptr<StoragePutRequest>> active_;
  CURLM *multi_{nullptr};
  bool stopping_{false};
  std::thread worker_;
};

namespace {

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

  cookieShare_ = curl_share_init();
  if (cookieShare_ == nullptr ||
      curl_share_setopt(cookieShare_, CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE) !=
          CURLSHE_OK ||
      curl_share_setopt(cookieShare_, CURLSHOPT_LOCKFUNC, lockCookieShare) !=
          CURLSHE_OK ||
      curl_share_setopt(cookieShare_, CURLSHOPT_UNLOCKFUNC, unlockCookieShare) !=
          CURLSHE_OK ||
      curl_share_setopt(cookieShare_, CURLSHOPT_USERDATA, &cookieMutex_) !=
          CURLSHE_OK) {
    if (cookieShare_ != nullptr) {
      curl_share_cleanup(cookieShare_);
      cookieShare_ = nullptr;
    }
    throw std::runtime_error("failed to initialize curl cookie sharing");
  }

  try {
    storagePutTransport_ = std::make_unique<StoragePutTransport>();
  } catch (...) {
    curl_share_cleanup(cookieShare_);
    cookieShare_ = nullptr;
    throw;
  }
}

ArkiveHttpClient::~ArkiveHttpClient() {
  storagePutTransport_.reset();
  if (cookieShare_ != nullptr) {
    curl_share_cleanup(cookieShare_);
  }
}

ArkiveHttpClient::ArkiveHttpClient(ArkiveHttpClient &&other) noexcept
    : baseUrl_(std::move(other.baseUrl_)),
      cookiePath_(std::move(other.cookiePath_)),
      cookieShare_(other.cookieShare_),
      storagePutTransport_(std::move(other.storagePutTransport_)) {
  if (cookieShare_ != nullptr) {
    curl_share_setopt(cookieShare_, CURLSHOPT_USERDATA, &cookieMutex_);
  }
  other.cookieShare_ = nullptr;
}

ArkiveHttpClient &ArkiveHttpClient::operator=(ArkiveHttpClient &&other) noexcept {
  if (this != &other) {
    baseUrl_ = std::move(other.baseUrl_);
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

void ArkiveHttpClient::configureCurl(CURL *curl) const {
  curl_easy_setopt(curl, CURLOPT_SHARE, cookieShare_);
  curl_easy_setopt(curl, CURLOPT_COOKIEFILE, cookiePath_.c_str());
  curl_easy_setopt(curl, CURLOPT_COOKIEJAR, cookiePath_.c_str());
}

static size_t writeCallback(char *ptr, size_t size, size_t nmemb,
                            void *userdata) {
  auto *output = static_cast<std::string *>(userdata);
  output->append(ptr, size * nmemb);
  return size * nmemb;
}

struct SinkContext {
  const ArkiveHttpClient::ByteSink *sink;
};

static size_t sinkCallback(char *ptr, size_t size, size_t nmemb,
                           void *userdata) {
  auto *ctx = static_cast<SinkContext *>(userdata);
  const size_t total = size * nmemb;
  (*ctx->sink)(reinterpret_cast<const uint8_t *>(ptr), total);
  return total;
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
  configureCurl(curl.get());

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

  CURLcode code = curl_easy_perform(curl.get());
  curl_easy_setopt(curl.get(), CURLOPT_COOKIELIST, "FLUSH");

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
  configureCurl(curl.get());

  std::string response;
  std::string requestUrl = url(path);

  HeaderPtr headers(nullptr, &curl_slist_free_all);
  appendHeader(headers, "Accept: application/json");

  curl_easy_setopt(curl.get(), CURLOPT_URL, requestUrl.c_str());
  curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
  curl_easy_setopt(curl.get(), CURLOPT_HTTPGET, 1L);
  curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, writeCallback);
  curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);

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
  configureCurl(curl.get());

  std::string response;
  std::string requestUrl = url(path);

  curl_easy_setopt(curl.get(), CURLOPT_URL, requestUrl.c_str());
  curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
  curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, "");
  curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, 0L);
  curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, writeCallback);
  curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);

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
                                       const std::vector<uint8_t> &body) {
  const std::string requestUrl = url(pathOrUrl);
  return storagePutTransport_->put(requestUrl, body);
}

void ArkiveHttpClient::getRangeToSink(const std::string &pathOrUrl,
                                      uint64_t offset, uint64_t length,
                                      const ByteSink &sink) {
  if (length == 0) {
    return;
  }

  CurlPtr curl = makeCurlHandle();
  configureCurl(curl.get());
  std::string response;
  const std::string requestUrl = url(pathOrUrl);
  const std::string range =
      std::to_string(offset) + "-" + std::to_string(offset + length - 1);
  SinkContext ctx{&sink};

  curl_easy_setopt(curl.get(), CURLOPT_URL, requestUrl.c_str());
  curl_easy_setopt(curl.get(), CURLOPT_HTTPGET, 1L);
  curl_easy_setopt(curl.get(), CURLOPT_RANGE, range.c_str());
  curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, sinkCallback);
  curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &ctx);

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

  if (status != 206) {
    throw HttpError(static_cast<int>(status), response);
  }
}
