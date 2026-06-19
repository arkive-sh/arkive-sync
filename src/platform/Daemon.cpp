#include "platform/Daemon.hpp"

#include "api/ArkiveApi.hpp"
#include "api/ArkiveHttpClient.hpp"
#include "crypto/RustCrypto.hpp"
#include "db/Sqlite.hpp"
#include "fs/FileWatcher.hpp"
#include "platform/AppDataPaths.hpp"
#include "repo/DirtyPathRepo.hpp"
#include "repo/EntryRepo.hpp"
#include "repo/QueueRepo.hpp"
#include "repo/ScanRepo.hpp"
#include "repo/SyncRepo.hpp"
#include "repo/UploadResumeRepo.hpp"
#include "repo/UserRepo.hpp"
#include "service/FolderCreateWorker.hpp"
#include "service/QueueService.hpp"
#include "service/RemoteSyncService.hpp"
#include "service/SyncService.hpp"
#include "service/UploadJobRunner.hpp"
#include "service/UploadService.hpp"
#include "service/VaultService.hpp"
#include "fs/FileEncryptor.hpp"
#include "sync/RemoteScanner.hpp"
#include "sync/RootScanner.hpp"

#if defined(__linux__)
#include "platform/linux/daemon/LinuxDaemon.hpp"
#endif

std::unique_ptr<Daemon> Daemon::create() {
#if defined(__linux__)
  auto db = std::make_unique<Database>();
  auto crypto = std::make_unique<RustCrypto>();
  auto syncRepo = std::make_unique<SyncRepo>(db->getDb());
  auto scanRepo = std::make_unique<ScanRepo>(db->getDb());
  auto dirtyPathRepo = std::make_unique<DirtyPathRepo>(db->getDb());
  auto entryRepo = std::make_unique<EntryRepo>(db->getDb());
  auto queueRepo = std::make_unique<QueueRepo>(db->getDb());
  auto userRepo = std::make_unique<UserRepo>(db->getDb());
  auto uploadResumeRepo = std::make_unique<UploadResumeRepo>(db->getDb());
  auto syncService = std::make_unique<SyncService>(*syncRepo, *crypto);
  auto vaultService = std::make_unique<VaultService>(*userRepo, *crypto);
  auto fileEncryptor =
      std::make_unique<FileEncryptor>(*crypto, *vaultService);
  auto rootScanner = std::make_unique<RootScanner>(
      db->getDb(), *crypto, *syncService, *scanRepo, *dirtyPathRepo,
      *entryRepo);
  auto watcher = IFileWatcher::create();
  std::unique_ptr<ArkiveHttpClient> client;
  std::unique_ptr<ArkiveApi> api;
  std::unique_ptr<FolderCreateWorker> folderCreateWorker;
  std::unique_ptr<UploadService> uploadService;
  std::unique_ptr<UploadJobRunner> uploadJobRunner;
  std::unique_ptr<RemoteScanner> remoteScanner;

  if (const auto account = userRepo->getAccount();
      account.has_value() && !account->baseUrl.empty()) {
    client = std::make_unique<ArkiveHttpClient>(account->baseUrl,
                                                cookieJarPath().string());
    api = std::make_unique<ArkiveApi>(*client);
    folderCreateWorker = std::make_unique<FolderCreateWorker>(
        *syncRepo, *entryRepo, *userRepo, *fileEncryptor, *api);
    uploadService = std::make_unique<UploadService>(*api, *fileEncryptor,
                                                    *uploadResumeRepo);
    uploadJobRunner = std::make_unique<UploadJobRunner>(
        *syncRepo, *entryRepo, *uploadService);
    remoteScanner =
        std::make_unique<RemoteScanner>(*syncRepo, *entryRepo, *api);
  }
  auto queueService = std::make_unique<QueueService>(
      *entryRepo, *queueRepo, *syncRepo, folderCreateWorker.get(),
      uploadJobRunner.get());
  auto remoteSyncService =
      std::make_unique<RemoteSyncService>(*entryRepo, remoteScanner.get());

  return std::make_unique<LinuxDaemon>(
      std::move(db), std::move(crypto), std::move(syncRepo),
      std::move(scanRepo), std::move(dirtyPathRepo), std::move(entryRepo),
      std::move(queueRepo), std::move(queueService),
      std::move(remoteSyncService),
      std::move(userRepo), std::move(uploadResumeRepo),
      std::move(vaultService), std::move(fileEncryptor), std::move(client),
      std::move(api), std::move(folderCreateWorker), std::move(uploadService),
      std::move(uploadJobRunner), std::move(syncService), std::move(rootScanner),
      std::move(watcher));
#elif defined(__APPLE__)
  throw std::runtime_error("Daemon is not implemented on macOS yet");
#elif defined(_WIN32)
  throw std::runtime_error("Daemon is not implemented on Windows yet");
#else
  throw std::runtime_error("Daemon is not implemented on this platform");
#endif
}
