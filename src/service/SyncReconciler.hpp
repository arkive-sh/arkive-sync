#pragma once

#include <filesystem>

struct SyncRoot;

class DownloadService;
struct Entry;
class ConflictRepo;
class EntryRepo;
class RustCrypto;

class SyncReconciler {
public:
  explicit SyncReconciler(EntryRepo &entryRepo);
  SyncReconciler(EntryRepo &entryRepo, DownloadService *downloadService);
  SyncReconciler(EntryRepo &entryRepo, ConflictRepo &conflictRepo,
                 DownloadService *downloadService, RustCrypto *crypto);

  void reconcileRoot(const SyncRoot &root);

private:
  void applyDeleteLocal(const SyncRoot &root, const std::filesystem::path &path,
                        bool isDirectory);
  std::filesystem::path conflictPathFor(const std::filesystem::path &path) const;
  void applyConflict(const SyncRoot &root, const Entry &entry,
                     const std::filesystem::path &path);

  EntryRepo &entryRepo_;
  ConflictRepo *conflictRepo_{nullptr};
  DownloadService *downloadService_{nullptr};
  RustCrypto *crypto_{nullptr};
};
