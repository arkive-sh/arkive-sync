#pragma once

#include <functional>

#include <condition_variable>
#include <mutex>
#include <thread>

class QueueWorker {
public:
  explicit QueueWorker(std::function<void()> onUploadsCompleted = {});
  ~QueueWorker();

  QueueWorker(const QueueWorker &) = delete;
  QueueWorker &operator=(const QueueWorker &) = delete;

  void setOnUploadsCompleted(std::function<void()> onUploadsCompleted);
  void trigger();
  void stop();

private:
  void run();

  std::function<void()> onUploadsCompleted_;
  std::mutex mutex_;
  std::condition_variable condition_;
  std::thread workerThread_;
  bool running_{true};
  bool pendingRun_{false};
};
