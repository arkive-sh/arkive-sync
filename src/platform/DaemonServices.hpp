#pragma once

#include <memory>

class IFileWatcher;
class Database;
class RustCrypto;
class SyncRepo;
class ScanRepo;
class DirtyPathRepo;
class EntryRepo;
class LocalEntryRepo;
class RemoteEntryRepo;
class ConflictRepo;
class QueueRepo;
class QueueService;
class RemoteSyncService;
class FolderCreateWorker;
class UserRepo;
class AuthService;
class VaultService;
class FileEncryptor;
class ArkiveHttpClient;
class ArkiveApi;
class UploadResumeRepo;
class UploadService;
class UploadJobRunner;
class SyncService;
class SyncReconciler;
class RootScanner;
class DownloadRecordDecryptor;
class DownloadService;

struct DaemonServices {
  std::unique_ptr<Database> db;
  std::unique_ptr<RustCrypto> crypto;
  std::unique_ptr<SyncRepo> syncRepo;
  std::unique_ptr<ScanRepo> scanRepo;
  std::unique_ptr<DirtyPathRepo> dirtyPathRepo;
  std::unique_ptr<EntryRepo> entryRepo;
  std::unique_ptr<LocalEntryRepo> localEntryRepo;
  std::unique_ptr<RemoteEntryRepo> remoteEntryRepo;
  std::unique_ptr<ConflictRepo> conflictRepo;
  std::unique_ptr<QueueRepo> queueRepo;
  std::unique_ptr<QueueService> queueService;
  std::unique_ptr<RemoteSyncService> remoteSyncService;
  std::unique_ptr<UserRepo> userRepo;
  std::unique_ptr<AuthService> authService;
  std::unique_ptr<UploadResumeRepo> uploadResumeRepo;
  std::unique_ptr<VaultService> vaultService;
  std::unique_ptr<FileEncryptor> fileEncryptor;
  std::unique_ptr<ArkiveHttpClient> client;
  std::unique_ptr<ArkiveApi> api;
  std::unique_ptr<FolderCreateWorker> folderCreateWorker;
  std::unique_ptr<UploadService> uploadService;
  std::unique_ptr<UploadJobRunner> uploadJobRunner;
  std::unique_ptr<DownloadRecordDecryptor> downloadRecordDecryptor;
  std::unique_ptr<DownloadService> downloadService;
  std::unique_ptr<SyncService> syncService;
  std::unique_ptr<SyncReconciler> syncReconciler;
  std::unique_ptr<RootScanner> rootScanner;
  std::unique_ptr<IFileWatcher> watcher;

  DaemonServices();
  ~DaemonServices();
  DaemonServices(DaemonServices &&) noexcept;
  DaemonServices &operator=(DaemonServices &&) noexcept;
  DaemonServices(const DaemonServices &) = delete;
  DaemonServices &operator=(const DaemonServices &) = delete;
};
