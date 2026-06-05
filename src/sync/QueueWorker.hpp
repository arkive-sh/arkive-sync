#pragma once

#include <condition_variable>
#include <mutex>
#include <thread>

class QueueWorker {
public:
  QueueWorker();
  ~QueueWorker();

  QueueWorker(const QueueWorker &) = delete;
  QueueWorker &operator=(const QueueWorker &) = delete;

  void trigger();
  void stop();

private:
  void run();

  std::mutex mutex_;
  std::condition_variable condition_;
  std::thread workerThread_;
  bool running_{true};
  bool pendingRun_{false};
};
