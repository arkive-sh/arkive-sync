#pragma once

#include "repo/SyncRepo.hpp"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

class RemoteScanner;

class RemoteSyncWorker {
public:
  explicit RemoteSyncWorker(RemoteScanner &scanner, SyncRepo &syncRepo);
  ~RemoteSyncWorker();

  RemoteSyncWorker(const RemoteSyncWorker &) = delete;
  RemoteSyncWorker &operator=(const RemoteSyncWorker &) = delete;

  void start();
  void stop();
  void trigger();

private:
  void runLoop();

  RemoteScanner &scanner_;
  SyncRepo syncRepo_;
  std::atomic<bool> running_{false};
  std::thread worker_;
  std::condition_variable cv_;
  std::mutex mutex_;
  bool triggered_{false};
};
