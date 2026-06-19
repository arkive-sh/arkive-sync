#pragma once

#include "platform/Daemon.hpp"

#include <memory>

class IFileWatcher;
class Database;
class RustCrypto;
class SyncRepo;
class ScanRepo;
class DirtyPathRepo;
class EntryRepo;
class QueueRepo;
class QueueService;
class RemoteSyncService;
class FolderCreateWorker;
class UserRepo;
class VaultService;
class FileEncryptor;
class ArkiveHttpClient;
class ArkiveApi;
class UploadResumeRepo;
class UploadService;
class UploadJobRunner;
class SyncService;
class RootScanner;

class LinuxDaemon final : public Daemon {
public:
  LinuxDaemon(std::unique_ptr<Database> db, std::unique_ptr<RustCrypto> crypto,
              std::unique_ptr<SyncRepo> syncRepo,
              std::unique_ptr<ScanRepo> scanRepo,
              std::unique_ptr<DirtyPathRepo> dirtyPathRepo,
              std::unique_ptr<EntryRepo> entryRepo,
              std::unique_ptr<QueueRepo> queueRepo,
              std::unique_ptr<QueueService> queueService,
              std::unique_ptr<RemoteSyncService> remoteSyncService,
              std::unique_ptr<UserRepo> userRepo,
              std::unique_ptr<UploadResumeRepo> uploadResumeRepo,
              std::unique_ptr<VaultService> vaultService,
              std::unique_ptr<FileEncryptor> fileEncryptor,
              std::unique_ptr<ArkiveHttpClient> client,
              std::unique_ptr<ArkiveApi> api,
              std::unique_ptr<FolderCreateWorker> folderCreateWorker,
              std::unique_ptr<UploadService> uploadService,
              std::unique_ptr<UploadJobRunner> uploadJobRunner,
              std::unique_ptr<SyncService> syncService,
              std::unique_ptr<RootScanner> rootScanner,
              std::unique_ptr<IFileWatcher> watcher);
  ~LinuxDaemon() override;

  int run() override;

private:
  std::unique_ptr<Database> db_;
  std::unique_ptr<RustCrypto> crypto_;
  std::unique_ptr<SyncRepo> syncRepo_;
  std::unique_ptr<ScanRepo> scanRepo_;
  std::unique_ptr<DirtyPathRepo> dirtyPathRepo_;
  std::unique_ptr<EntryRepo> entryRepo_;
  std::unique_ptr<QueueRepo> queueRepo_;
  std::unique_ptr<QueueService> queueService_;
  std::unique_ptr<RemoteSyncService> remoteSyncService_;
  std::unique_ptr<UserRepo> userRepo_;
  std::unique_ptr<UploadResumeRepo> uploadResumeRepo_;
  std::unique_ptr<VaultService> vaultService_;
  std::unique_ptr<FileEncryptor> fileEncryptor_;
  std::unique_ptr<ArkiveHttpClient> client_;
  std::unique_ptr<ArkiveApi> api_;
  std::unique_ptr<FolderCreateWorker> folderCreateWorker_;
  std::unique_ptr<UploadService> uploadService_;
  std::unique_ptr<UploadJobRunner> uploadJobRunner_;
  std::unique_ptr<SyncService> syncService_;
  std::unique_ptr<RootScanner> rootScanner_;
  std::unique_ptr<IFileWatcher> watcher_;
};
