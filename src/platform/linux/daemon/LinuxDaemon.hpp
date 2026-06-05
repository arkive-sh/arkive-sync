#pragma once

#include "platform/Daemon.hpp"

#include <memory>

class IFileWatcher;
class ScanWorker;
class SyncScheduler;
class SyncService;

class LinuxDaemon final : public Daemon {
public:
  LinuxDaemon(SyncScheduler &syncScheduler, SyncService &syncService,
              std::unique_ptr<IFileWatcher> watcher);
  ~LinuxDaemon() override;

  int run() override;

private:
  SyncScheduler &syncScheduler_;
  std::unique_ptr<IFileWatcher> watcher_;
  std::unique_ptr<ScanWorker> scanWorker_;
};
