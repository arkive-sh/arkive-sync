#include "sync/ScanWorker.hpp"

#include "crypto/RustCrypto.hpp"
#include "db/Sqlite.hpp"
#include "helpers/LocalPathProtector.hpp"
#include "repo/SyncRepo.hpp"
#include "repo/UserRepo.hpp"
#include "service/SyncService.hpp"
#include "service/VaultService.hpp"

#include <platform/AppDataPaths.hpp>

#include <exception>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <utility>

namespace {

struct ScanWorkerContext {
  // This worker runs on its own thread, so it builds a thread-local service
  // graph with its own SQLite connection instead of sharing main-thread repos.
  Database db;
  UserRepo userRepo;
  RustCrypto crypto;
  VaultService vaultService;
  LocalPathProtector pathProtector;
  SyncRepo syncRepo;
  SyncService syncService;

  ScanWorkerContext()
      : db(databasePath()), userRepo(db.getDb()), crypto(),
        vaultService(userRepo, crypto), pathProtector(crypto, vaultService),
        syncRepo(db.getDb(), pathProtector), syncService(syncRepo, crypto) {}
};

} // namespace

ScanWorker::ScanWorker(std::function<void()> onJobFinished)
    : onJobFinished_(std::move(onJobFinished)),
      workerThread_(&ScanWorker::run, this) {}

ScanWorker::~ScanWorker() { stop(); }

void ScanWorker::enqueue(SyncScheduler::ScanJob job) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_) {
      return;
    }

    const std::string key = jobKey(job);
    if (queuedOrRunningJobKeys_.contains(key)) {
      spdlog::info("ScanWorker suppressed duplicate job root={} path={}",
                   job.rootId, job.path.string());
      return;
    }

    queuedOrRunningJobKeys_.insert(std::move(key));
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
  try {
    ScanWorkerContext context;

    while (true) {
      SyncScheduler::ScanJob job;
      std::string key;

      {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait(lock, [&] { return !running_ || !queue_.empty(); });

        if (!running_ && queue_.empty()) {
          return;
        }

        job = std::move(queue_.front());
        queue_.pop_front();
        key = jobKey(job);
      }

      try {
        if (job.type == SyncScheduler::ScanJobType::Root) {
          spdlog::info("ScanWorker running root scan path={}", job.path.string());
          const size_t changedEntries = context.syncService.scanRoot(job.path);
          spdlog::info("ScanWorker finished root scan path={} changed={}",
                       job.path.string(), changedEntries);
        } else {
          spdlog::info("ScanWorker running path scan root={} path={}", job.rootId,
                       job.path.string());
          const size_t changedEntries =
              context.syncService.scanPath(job.rootId, job.path);
          spdlog::info("ScanWorker finished path scan root={} path={} changed={}",
                       job.rootId, job.path.string(), changedEntries);
        }
      } catch (const std::exception &ex) {
        spdlog::error("ScanWorker failed job root={} path={}: {}", job.rootId,
                      job.path.string(), ex.what());
      }

      {
        std::lock_guard<std::mutex> lock(mutex_);
        queuedOrRunningJobKeys_.erase(key);
        if (!queue_.empty()) {
          key.clear();
        }
      }

      if (!key.empty() && onJobFinished_) {
        onJobFinished_();
      }
    }
  } catch (const std::exception &ex) {
    spdlog::error("ScanWorker failed to initialize: {}", ex.what());
  }
}

std::string ScanWorker::jobKey(const SyncScheduler::ScanJob &job) {
  const std::filesystem::path normalizedPath =
      job.path.empty() ? std::filesystem::path{}
                       : std::filesystem::absolute(job.path).lexically_normal();
  return std::to_string(static_cast<int>(job.type)) + "\n" + job.rootId + "\n" +
         normalizedPath.string();
}
