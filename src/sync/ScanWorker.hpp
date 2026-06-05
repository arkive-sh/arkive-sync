#pragma once

#include "service/SyncScheduler.hpp"

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_set>

class ScanWorker {
public:
  explicit ScanWorker(std::function<void()> onJobFinished);
  ~ScanWorker();

  ScanWorker(const ScanWorker &) = delete;
  ScanWorker &operator=(const ScanWorker &) = delete;

  void enqueue(SyncScheduler::ScanJob job);
  void stop();

private:
  void run();
  static std::string jobKey(const SyncScheduler::ScanJob &job);

  std::function<void()> onJobFinished_;
  std::mutex mutex_;
  std::condition_variable condition_;
  std::deque<SyncScheduler::ScanJob> queue_;
  std::unordered_set<std::string> queuedOrRunningJobKeys_;
  std::thread workerThread_;
  bool running_{true};
};
