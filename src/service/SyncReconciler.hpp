#pragma once

#include <filesystem>

struct SyncRoot;

class EntryRepo;

class SyncReconciler {
public:
  explicit SyncReconciler(EntryRepo &entryRepo);

  void reconcileRoot(const SyncRoot &root);

private:
  void applyDeleteLocal(const SyncRoot &root, const std::filesystem::path &path,
                        bool isDirectory);

  EntryRepo &entryRepo_;
};
