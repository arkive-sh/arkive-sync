#include "sync/QueueWorker.hpp"

#include "api/ArkiveApi.hpp"
#include "api/ArkiveHttpClient.hpp"
#include "crypto/RustCrypto.hpp"
#include "db/Sqlite.hpp"
#include "fs/FileEncryptor.hpp"
#include "helpers/LocalPathProtector.hpp"
#include "platform/AppDataPaths.hpp"
#include "repo/QueueRepo.hpp"
#include "repo/SyncRepo.hpp"
#include "repo/UploadResumeRepo.hpp"
#include "repo/UserRepo.hpp"
#include "service/QueueService.hpp"
#include "service/SyncService.hpp"
#include "service/UploadJobRunner.hpp"
#include "service/UploadService.hpp"
#include "service/VaultService.hpp"

#include <spdlog/spdlog.h>
#include <stdexcept>

namespace {

struct QueueWorkerContext {
  // This worker runs on its own thread, so it builds a thread-local service
  // graph with its own SQLite connection instead of sharing main-thread repos.
  Database db;
  UserRepo userRepo;
  RustCrypto crypto;
  VaultService vaultService;
  LocalPathProtector pathProtector;
  SyncRepo syncRepo;
  QueueRepo queueRepo;
  UploadResumeRepo uploadResumeRepo;
  ArkiveHttpClient client;
  ArkiveApi api;
  FileEncryptor fileEncryptor;
  UploadService uploadService;
  UploadJobRunner uploadJobRunner;
  SyncService syncService;
  QueueService queueService;

  QueueWorkerContext()
      : db(databasePath()), userRepo(db.getDb()), crypto(),
        vaultService(userRepo, crypto), pathProtector(crypto, vaultService),
        syncRepo(db.getDb(), pathProtector), queueRepo(db.getDb()),
        uploadResumeRepo(db.getDb()),
        client(loadBaseUrl(userRepo), cookieJarPath().string()), api(client),
        fileEncryptor(crypto, vaultService),
        uploadService(api, fileEncryptor, uploadResumeRepo),
        uploadJobRunner(syncRepo, uploadService), syncService(syncRepo, crypto),
        queueService(queueRepo, syncRepo, uploadJobRunner, syncService, api) {}

  static std::string loadBaseUrl(UserRepo &userRepo) {
    const auto account = userRepo.getAccount();
    if (!account.has_value() || account->baseUrl.empty()) {
      throw std::runtime_error("Base URL is missing");
    }
    return account->baseUrl;
  }
};

} // namespace

QueueWorker::QueueWorker() : workerThread_(&QueueWorker::run, this) {}

QueueWorker::~QueueWorker() { stop(); }

void QueueWorker::trigger() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    pendingRun_ = true;
  }
  condition_.notify_one();
}

void QueueWorker::stop() {
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

void QueueWorker::run() {
  try {
    QueueWorkerContext context;

    while (true) {
      {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait(lock, [&] { return !running_ || pendingRun_; });

        if (!running_ && !pendingRun_) {
          return;
        }

        pendingRun_ = false;
      }

      try {
        spdlog::info("QueueWorker processing queued uploads");
        context.queueService.processQueuedUploads();
      } catch (const std::exception &ex) {
        spdlog::error("QueueWorker failed: {}", ex.what());
      }
    }
  } catch (const std::exception &ex) {
    spdlog::error("QueueWorker failed to initialize: {}", ex.what());
  }
}
