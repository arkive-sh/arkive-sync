#pragma once

#include "service/SyncScheduler.hpp"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

class SyncService;

class ScanWorker {
public:
  explicit ScanWorker(SyncService &syncService);
  ~ScanWorker();

  ScanWorker(const ScanWorker &) = delete;
  ScanWorker &operator=(const ScanWorker &) = delete;

  void enqueue(SyncScheduler::ScanJob job);
  void stop();

private:
  void run();

  SyncService &syncService_;
  std::mutex mutex_;
  std::condition_variable condition_;
  std::deque<SyncScheduler::ScanJob> queue_;
  std::thread workerThread_;
  bool running_{true};
};
