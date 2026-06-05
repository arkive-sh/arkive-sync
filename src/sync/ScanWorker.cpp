#include "sync/ScanWorker.hpp"

#include "service/SyncService.hpp"

#include <spdlog/spdlog.h>
#include <utility>

ScanWorker::ScanWorker(SyncService &syncService)
    : syncService_(syncService), workerThread_(&ScanWorker::run, this) {}

ScanWorker::~ScanWorker() { stop(); }

void ScanWorker::enqueue(SyncScheduler::ScanJob job) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push_back(std::move(job));
  }
  condition_.notify_one();
}

void ScanWorker::stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_) {
      return;
    }
    running_ = false;
  }

  condition_.notify_all();
  if (workerThread_.joinable()) {
    workerThread_.join();
  }
}

void ScanWorker::run() {
  while (true) {
    SyncScheduler::ScanJob job;

    {
      std::unique_lock<std::mutex> lock(mutex_);
      condition_.wait(lock, [&] { return !running_ || !queue_.empty(); });

      if (!running_ && queue_.empty()) {
        return;
      }

      job = std::move(queue_.front());
      queue_.pop_front();
    }

    try {
      if (job.type == SyncScheduler::ScanJobType::Root) {
        spdlog::info("ScanWorker running root scan path={}", job.path.string());
        const size_t changedEntries = syncService_.scanRoot(job.path);
        spdlog::info("ScanWorker finished root scan path={} changed={}",
                     job.path.string(), changedEntries);
        continue;
      }

      spdlog::info("ScanWorker running path scan root={} path={}", job.rootId,
                   job.path.string());
      const size_t changedEntries = syncService_.scanPath(job.rootId, job.path);
      spdlog::info("ScanWorker finished path scan root={} path={} changed={}",
                   job.rootId, job.path.string(), changedEntries);
    } catch (const std::exception &ex) {
      spdlog::error("ScanWorker failed job root={} path={}: {}", job.rootId,
                    job.path.string(), ex.what());
    }
  }
}
