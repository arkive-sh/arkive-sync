#pragma once

#include <filesystem>

struct SyncRoot;

class DownloadService;
class EntryRepo;
class RustCrypto;

class SyncReconciler {
public:
  explicit SyncReconciler(EntryRepo &entryRepo);
  SyncReconciler(EntryRepo &entryRepo, DownloadService *downloadService);
  SyncReconciler(EntryRepo &entryRepo, DownloadService *downloadService,
                 RustCrypto *crypto);

  void reconcileRoot(const SyncRoot &root);

private:
  void applyDeleteLocal(const SyncRoot &root, const std::filesystem::path &path,
                        bool isDirectory);

  EntryRepo &entryRepo_;
  DownloadService *downloadService_{nullptr};
  RustCrypto *crypto_{nullptr};
};
