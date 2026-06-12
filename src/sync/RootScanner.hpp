#pragma once

#include "crypto/RustCrypto.hpp"
#include "fs/FileWatcher.hpp"
#include "repo/DirtyPathRepo.hpp"
#include "repo/EntryRepo.hpp"
#include "repo/ScanRepo.hpp"
#include "service/SyncService.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

class RootScanner {
public:
  RootScanner(RustCrypto &crypto, SyncService &syncSvc, ScanRepo &scanRepo,
              DirtyPathRepo &dirtyPathRepo, EntryRepo &entryRepo);
  RootScanner(RustCrypto &crypto, SyncService &syncSvc, ScanRepo &scanRepo,
              DirtyPathRepo &dirtyPathRepo, EntryRepo &entryRepo,
              std::unique_ptr<IFileWatcher> watcher);

  bool scanRoot(const std::string &syncRootId);

private:
  bool handleFileEntry(const std::string &syncRootId, const ScanJob &job,
                       const std::filesystem::path &absPath,
                       const std::string &relativePath,
                       std::error_code &ec);

  std::unique_ptr<IFileWatcher> watcher_;
  RustCrypto &crypto_;
  SyncService &syncSvc_;
  ScanRepo &scanRepo_;
  DirtyPathRepo &dirtyPathRepo_;
  EntryRepo &entryRepo_;
};
