#pragma once

#include "fs/FileWatcher.hpp"
#include "repo/DirtyPathRepo.hpp"
#include "repo/ScanRepo.hpp"
#include "service/SyncService.hpp"

#include <memory>
#include <string>

class RootScanner {
public:
  explicit RootScanner(SyncService &syncSvc);
  RootScanner(SyncService &syncSvc, std::unique_ptr<IFileWatcher> watcher);

  bool scanRoot(const std::string &syncRootId);

private:
  std::unique_ptr<IFileWatcher> watcher_;
  SyncService &syncSvc_;
};
