#pragma once

#include "platform/Daemon.hpp"

#include <memory>

class IFileWatcher;
class Database;
class RustCrypto;
class SyncRepo;
class ScanRepo;
class DirtyPathRepo;
class EntryRepo;
class SyncService;
class RootScanner;

class LinuxDaemon final : public Daemon {
public:
  LinuxDaemon(std::unique_ptr<Database> db, std::unique_ptr<RustCrypto> crypto,
              std::unique_ptr<SyncRepo> syncRepo,
              std::unique_ptr<ScanRepo> scanRepo,
              std::unique_ptr<DirtyPathRepo> dirtyPathRepo,
              std::unique_ptr<EntryRepo> entryRepo,
              std::unique_ptr<SyncService> syncService,
              std::unique_ptr<RootScanner> rootScanner,
              std::unique_ptr<IFileWatcher> watcher);
  ~LinuxDaemon() override;

  int run() override;

private:
  std::unique_ptr<Database> db_;
  std::unique_ptr<RustCrypto> crypto_;
  std::unique_ptr<SyncRepo> syncRepo_;
  std::unique_ptr<ScanRepo> scanRepo_;
  std::unique_ptr<DirtyPathRepo> dirtyPathRepo_;
  std::unique_ptr<EntryRepo> entryRepo_;
  std::unique_ptr<SyncService> syncService_;
  std::unique_ptr<RootScanner> rootScanner_;
  std::unique_ptr<IFileWatcher> watcher_;
};
