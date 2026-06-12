#pragma once

#include "crypto/RustCrypto.hpp"
#include "fs/FileWatcher.hpp"
#include "repo/DirtyPathRepo.hpp"
#include "repo/ScanRepo.hpp"
#include "service/SyncService.hpp"

#include <memory>
#include <string>

class RootScanner {
public:
  RootScanner(RustCrypto &crypto, SyncService &syncSvc, ScanRepo &scanRepo,
              DirtyPathRepo &dirtyPathRepo);
  RootScanner(RustCrypto &crypto, SyncService &syncSvc, ScanRepo &scanRepo,
              DirtyPathRepo &dirtyPathRepo,
              std::unique_ptr<IFileWatcher> watcher);

  bool scanRoot(const std::string &syncRootId);

private:
  std::unique_ptr<IFileWatcher> watcher_;
  RustCrypto &crypto_;
  SyncService &syncSvc_;
  ScanRepo &scanRepo_;
  DirtyPathRepo &dirtyPathRepo_;
};
