#include "platform/Daemon.hpp"

#include "crypto/RustCrypto.hpp"
#include "db/Sqlite.hpp"
#include "fs/FileWatcher.hpp"
#include "repo/DirtyPathRepo.hpp"
#include "repo/EntryRepo.hpp"
#include "repo/ScanRepo.hpp"
#include "repo/SyncRepo.hpp"
#include "service/SyncService.hpp"
#include "sync/RootScanner.hpp"

#if defined(__linux__)
#include "platform/linux/daemon/LinuxDaemon.hpp"
#endif

std::unique_ptr<Daemon> Daemon::create() {
#if defined(__linux__)
  auto db = std::make_unique<Database>();
  auto crypto = std::make_unique<RustCrypto>();
  auto syncRepo = std::make_unique<SyncRepo>(db->getDb());
  auto scanRepo = std::make_unique<ScanRepo>(db->getDb());
  auto dirtyPathRepo = std::make_unique<DirtyPathRepo>(db->getDb());
  auto entryRepo = std::make_unique<EntryRepo>(db->getDb());
  auto syncService =
      std::make_unique<SyncService>(*syncRepo, *crypto);
  auto rootScanner = std::make_unique<RootScanner>(
      db->getDb(), *crypto, *syncService, *scanRepo, *dirtyPathRepo,
      *entryRepo);
  auto watcher = IFileWatcher::create();

  return std::make_unique<LinuxDaemon>(
      std::move(db), std::move(crypto), std::move(syncRepo),
      std::move(scanRepo), std::move(dirtyPathRepo), std::move(entryRepo),
      std::move(syncService), std::move(rootScanner), std::move(watcher));
#elif defined(__APPLE__)
  throw std::runtime_error("Daemon is not implemented on macOS yet");
#elif defined(_WIN32)
  throw std::runtime_error("Daemon is not implemented on Windows yet");
#else
  throw std::runtime_error("Daemon is not implemented on this platform");
#endif
}
