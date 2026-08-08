#include "networking/StoragePutTransport.hpp"

#include "api/HttpError.hpp"
#include <cctype>
#include <condition_variable>
#include <curl/curl.h>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>

namespace {

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

class networking::StoragePutTransport::Impl {
public:
  Impl() : worker_([this] { run(); }) {}

  ~Impl() {
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

namespace networking {

StoragePutTransport::StoragePutTransport() : impl_(std::make_unique<Impl>()) {}

StoragePutTransport::~StoragePutTransport() = default;

StoragePutTransport::StoragePutTransport(StoragePutTransport &&other) noexcept
    : impl_(std::move(other.impl_)) {}

StoragePutTransport &StoragePutTransport::operator=(
    StoragePutTransport &&other) noexcept {
  impl_ = std::move(other.impl_);
  return *this;
}

std::string StoragePutTransport::put(std::string requestUrl,
                                     const std::vector<uint8_t> &body) {
  return impl_->put(std::move(requestUrl), body);
}

} // namespace networking
