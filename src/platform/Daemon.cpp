#include "platform/Daemon.hpp"

#include "api/ArkiveApi.hpp"
#include "api/ArkiveHttpClient.hpp"
#include "crypto/RustCrypto.hpp"
#include "db/Sqlite.hpp"
#include "download/DownloadRecordDecryptor.hpp"
#include "download/DownloadService.hpp"
#include "fs/FileWatcher.hpp"
#include "platform/AppDataPaths.hpp"
#include "repo/ConflictRepo.hpp"
#include "repo/DirtyPathRepo.hpp"
#include "repo/EntryRepo.hpp"
#include "repo/LocalEntryRepo.hpp"
#include "repo/QueueRepo.hpp"
#include "repo/RemoteEntryRepo.hpp"
#include "repo/ScanRepo.hpp"
#include "repo/SyncRepo.hpp"
#include "repo/UploadResumeRepo.hpp"
#include "repo/UserRepo.hpp"
#include "service/FolderCreateWorker.hpp"
#include "service/QueueService.hpp"
#include "service/RemoteSyncService.hpp"
#include "service/SyncReconciler.hpp"
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

#if defined(__linux__)
namespace {

LinuxDaemonServices createLinuxDaemonServices() {
  LinuxDaemonServices services;

  services.db = std::make_unique<Database>();
  services.crypto = std::make_unique<RustCrypto>();
  services.syncRepo = std::make_unique<SyncRepo>(services.db->getDb());
  services.scanRepo = std::make_unique<ScanRepo>(services.db->getDb());
  services.dirtyPathRepo =
      std::make_unique<DirtyPathRepo>(services.db->getDb());
  services.entryRepo = std::make_unique<EntryRepo>(services.db->getDb());
  services.localEntryRepo =
      std::make_unique<LocalEntryRepo>(services.db->getDb());
  services.remoteEntryRepo =
      std::make_unique<RemoteEntryRepo>(services.db->getDb());
  services.conflictRepo = std::make_unique<ConflictRepo>(services.db->getDb());
  services.queueRepo = std::make_unique<QueueRepo>(services.db->getDb());
  services.userRepo = std::make_unique<UserRepo>(services.db->getDb());
  services.uploadResumeRepo =
      std::make_unique<UploadResumeRepo>(services.db->getDb());
  services.syncService =
      std::make_unique<SyncService>(*services.syncRepo, *services.crypto);
  services.vaultService =
      std::make_unique<VaultService>(*services.userRepo, *services.crypto);
  services.fileEncryptor =
      std::make_unique<FileEncryptor>(*services.crypto, *services.vaultService);
  services.rootScanner = std::make_unique<RootScanner>(
      services.db->getDb(), *services.crypto, *services.syncService,
      *services.scanRepo, *services.dirtyPathRepo, *services.entryRepo,
      *services.localEntryRepo);
  services.watcher = IFileWatcher::create();
  std::unique_ptr<RemoteScanner> remoteScanner;

  if (const auto account = services.userRepo->getAccount();
      account.has_value() && !account->baseUrl.empty()) {
    services.client = std::make_unique<ArkiveHttpClient>(
        account->baseUrl, cookieJarPath().string());
    services.api = std::make_unique<ArkiveApi>(*services.client);
    services.folderCreateWorker = std::make_unique<FolderCreateWorker>(
        *services.syncRepo, *services.entryRepo, *services.remoteEntryRepo,
        *services.userRepo, *services.fileEncryptor, *services.api);
    services.uploadService = std::make_unique<UploadService>(
        *services.api, *services.fileEncryptor, *services.uploadResumeRepo);
    services.uploadJobRunner = std::make_unique<UploadJobRunner>(
        *services.syncRepo, *services.entryRepo, *services.remoteEntryRepo,
        *services.uploadService);
    remoteScanner = std::make_unique<RemoteScanner>(
        *services.syncRepo, *services.entryRepo, *services.remoteEntryRepo,
        *services.api, *services.crypto, *services.vaultService,
        *services.userRepo);
    services.downloadRecordDecryptor =
        std::make_unique<DownloadRecordDecryptor>(*services.crypto,
                                                  *services.vaultService);
    services.downloadService = std::make_unique<DownloadService>(
        *services.api, *services.client, *services.crypto,
        *services.downloadRecordDecryptor);
  }

  services.queueService = std::make_unique<QueueService>(
      *services.entryRepo, *services.queueRepo, *services.syncRepo,
      services.folderCreateWorker.get(), services.uploadJobRunner.get());
  services.syncReconciler = std::make_unique<SyncReconciler>(
      *services.entryRepo, *services.conflictRepo, *services.remoteEntryRepo,
      services.downloadService.get(), services.crypto.get());
  services.remoteSyncService = std::make_unique<RemoteSyncService>(
      std::move(remoteScanner), *services.remoteEntryRepo, *services.syncRepo);

  return services;
}

} // namespace
#endif

std::unique_ptr<Daemon> Daemon::create() {
#if defined(__linux__)
  return std::make_unique<LinuxDaemon>(createLinuxDaemonServices());
#elif defined(__APPLE__)
  throw std::runtime_error("Daemon is not implemented on macOS yet");
#elif defined(_WIN32)
  throw std::runtime_error("Daemon is not implemented on Windows yet");
#else
  throw std::runtime_error("Daemon is not implemented on this platform");
#endif
}
