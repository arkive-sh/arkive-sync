#pragma once

#include "crypto/RustCrypto.hpp"
#include "fs/FileWatcher.hpp"
#include "repo/DirtyPathRepo.hpp"
#include "repo/EntryRepo.hpp"
#include "repo/LocalEntryRepo.hpp"
#include "repo/ScanRepo.hpp"
#include "service/SyncService.hpp"

#include <filesystem>
#include <memory>
#include <sqlite3.h>
#include <string>

class RootScanner {
public:
  RootScanner(sqlite3 *db, RustCrypto &crypto, SyncService &syncSvc,
              ScanRepo &scanRepo, DirtyPathRepo &dirtyPathRepo,
              EntryRepo &entryRepo, LocalEntryRepo &localEntryRepo);
  RootScanner(sqlite3 *db, RustCrypto &crypto, SyncService &syncSvc,
              ScanRepo &scanRepo, DirtyPathRepo &dirtyPathRepo,
              EntryRepo &entryRepo, LocalEntryRepo &localEntryRepo,
              std::unique_ptr<IFileWatcher> watcher);

  bool scanRoot(const std::string &syncRootId);
  bool scanPath(const std::string &rootId,
                const std::filesystem::path &relativePath);

private:
  bool handleFileEntry(const std::string &syncRootId, const ScanJob &job,
                       const std::filesystem::path &absPath,
                       const std::string &relativePath, std::error_code &ec);
  bool scanSubtree(const std::string &syncRootId, ScanJob job,
                   const std::filesystem::path &rootPath,
                   const std::filesystem::path &subtreePath,
                   const std::string &relativePath, std::error_code &ec);

  std::unique_ptr<IFileWatcher> watcher_;
  sqlite3 *db_;
  RustCrypto &crypto_;
  SyncService &syncSvc_;
  ScanRepo &scanRepo_;
  DirtyPathRepo &dirtyPathRepo_;
  EntryRepo &entryRepo_;
  LocalEntryRepo &localEntryRepo_;
};
