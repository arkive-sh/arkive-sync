#pragma once

#include "platform/Daemon.hpp"

#include <memory>

class IFileWatcher;
class ScanWorker;
class QueueWorker;
class SyncScheduler;

class LinuxDaemon final : public Daemon {
public:
  LinuxDaemon(SyncScheduler &syncScheduler, std::unique_ptr<IFileWatcher> watcher);
  ~LinuxDaemon() override;

  int run() override;

private:
  SyncScheduler &syncScheduler_;
  std::unique_ptr<IFileWatcher> watcher_;
  std::unique_ptr<ScanWorker> scanWorker_;
  std::unique_ptr<QueueWorker> queueWorker_;
};
