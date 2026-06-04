#include "crypto/Aad.hpp"
#include "crypto/RustCrypto.hpp"
#include "helpers/Base64.hpp"
#include "helpers/LocalPathProtector.hpp"
#include "repo/QueueRepo.hpp"
#include "repo/SyncRepo.hpp"
#include "repo/UserRepo.hpp"
#include "service/SyncScheduler.hpp"
#include "service/SyncService.hpp"
#include "service/VaultService.hpp"

#include "support/FakeSecureStorage.hpp"
#include "support/TestDatabase.hpp"
#include "support/TestFs.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <thread>
#include <vector>

namespace {

using TestDb = TestDatabase;

void seedUnlockedAccount(UserRepo &userRepo, VaultService &vaultService,
                         RustCrypto &crypto) {
  const std::string password = "test-password";
  const std::vector<uint8_t> salt = crypto.generateSalt();
  const std::vector<uint8_t> masterKey = crypto.generateMasterKey();
  const std::vector<uint8_t> encryptedMasterKey = crypto.wrapMasterKey(
      masterKey, crypto.derivePasswordKek(password, salt),
      ArkiveAad::toBytes(ArkiveAad::kMasterKey));

  userRepo.upsertAccount(AccountRecord{
      .baseUrl = "http://localhost:8080",
      .email = std::string("test@example.com"),
      .vaultSalt = encodeBase64(salt),
      .encryptedMasterKey = encodeBase64(encryptedMasterKey),
      .vaultSessionKeyId = std::nullopt,
      .vaultSessionBlob = std::nullopt,
  });

  vaultService.unlock(password);
}

WatchRoot onlyWatchRoot(SyncScheduler &scheduler) {
  const auto roots = scheduler.watchRoots();
  REQUIRE(roots.size() == 1);
  return roots.front();
}

EntryRecord onlyEntryForRoot(SyncRepo &syncRepo, const std::string &syncRootId) {
  const auto entries = syncRepo.getEntriesForSyncRoot(syncRootId);
  REQUIRE(entries.size() == 1);
  return entries.front();
}

} // namespace

TEST_CASE("SyncScheduler debounces repeated root events") {
  TestDb db;
  TempDir tempDir;
  RustCrypto crypto;
  UserRepo userRepo(db.get());
  VaultService vaultService(userRepo, crypto,
                            std::make_unique<FakeSecureStorage>());
  seedUnlockedAccount(userRepo, vaultService, crypto);
  LocalPathProtector pathProtector(crypto, vaultService);
  SyncRepo syncRepo(db.get(), pathProtector);
  QueueRepo queueRepo(db.get());
  SyncService syncService(syncRepo, crypto);

  syncService.addPath(tempDir.path());
  SyncScheduler scheduler(syncRepo, syncService, std::chrono::milliseconds(120),
                          std::chrono::milliseconds(500));

  const WatchRoot root = onlyWatchRoot(scheduler);
  writeFile(tempDir.path() / "movie.txt", "hello");

  scheduler.schedule(FileEvent{
      .rootId = root.rootId,
      .path = tempDir.path() / "movie.txt",
      .type = FileEventType::Modified,
      .isDirectory = false,
  });

  REQUIRE(scheduler.nextWaitTimeoutMs() >= 1);
  REQUIRE(scheduler.runDueScans().scannedPaths == 0);

  std::this_thread::sleep_for(std::chrono::milliseconds(70));
  scheduler.schedule(FileEvent{
      .rootId = root.rootId,
      .path = tempDir.path() / "movie.txt",
      .type = FileEventType::Modified,
      .isDirectory = false,
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(70));
  REQUIRE(scheduler.runDueScans().scannedPaths == 0);

  std::this_thread::sleep_for(std::chrono::milliseconds(70));
  const auto scanResult = scheduler.runDueScans();
  REQUIRE(scanResult.scannedRoots == 0);
  REQUIRE(scanResult.scannedPaths == 1);
  REQUIRE(scanResult.changedEntries == 1);

  const EntryRecord entry = onlyEntryForRoot(syncRepo, root.rootId);
  REQUIRE(entry.localPath == "movie.txt");
}

TEST_CASE("SyncScheduler max delay forces a scan under constant events") {
  TestDb db;
  TempDir tempDir;
  RustCrypto crypto;
  UserRepo userRepo(db.get());
  VaultService vaultService(userRepo, crypto,
                            std::make_unique<FakeSecureStorage>());
  seedUnlockedAccount(userRepo, vaultService, crypto);
  LocalPathProtector pathProtector(crypto, vaultService);
  SyncRepo syncRepo(db.get(), pathProtector);
  QueueRepo queueRepo(db.get());
  SyncService syncService(syncRepo, crypto);

  syncService.addPath(tempDir.path());
  SyncScheduler scheduler(syncRepo, syncService, std::chrono::milliseconds(120),
                          std::chrono::milliseconds(220));

  const WatchRoot root = onlyWatchRoot(scheduler);
  writeFile(tempDir.path() / "movie.txt", "hello");

  scheduler.schedule(FileEvent{
      .rootId = root.rootId,
      .path = tempDir.path() / "movie.txt",
      .type = FileEventType::Modified,
      .isDirectory = false,
  });

  for (int i = 0; i < 3; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    scheduler.schedule(FileEvent{
        .rootId = root.rootId,
        .path = tempDir.path() / "movie.txt",
        .type = FileEventType::Modified,
        .isDirectory = false,
    });
    REQUIRE(scheduler.runDueScans().scannedPaths == 0);
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(60));
  const auto scanResult = scheduler.runDueScans();
  REQUIRE(scanResult.scannedRoots == 0);
  REQUIRE(scanResult.scannedPaths == 1);
  REQUIRE(scanResult.changedEntries == 1);

  const EntryRecord entry = onlyEntryForRoot(syncRepo, root.rootId);
  REQUIRE(entry.localPath == "movie.txt");
}

TEST_CASE("SyncScheduler schedules both sides of a rename event") {
  TestDb db;
  TempDir tempDir;
  RustCrypto crypto;
  UserRepo userRepo(db.get());
  VaultService vaultService(userRepo, crypto,
                            std::make_unique<FakeSecureStorage>());
  seedUnlockedAccount(userRepo, vaultService, crypto);
  LocalPathProtector pathProtector(crypto, vaultService);
  SyncRepo syncRepo(db.get(), pathProtector);
  QueueRepo queueRepo(db.get());
  SyncService syncService(syncRepo, crypto);

  syncService.addPath(tempDir.path());
  SyncScheduler scheduler(syncRepo, syncService, std::chrono::milliseconds(30),
                          std::chrono::milliseconds(200));

  const WatchRoot root = onlyWatchRoot(scheduler);
  const auto oldPath = tempDir.path() / "old.txt";
  const auto newPath = tempDir.path() / "new.txt";
  writeFile(oldPath, "hello");

  REQUIRE(syncService.scanRoot(tempDir.path()) == 1);

  std::filesystem::rename(oldPath, newPath);

  scheduler.schedule(FileEvent{
      .rootId = root.rootId,
      .path = newPath,
      .oldPath = oldPath,
      .type = FileEventType::Renamed,
      .isDirectory = false,
      .cookie = 123,
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  const auto scanResult = scheduler.runDueScans();
  REQUIRE(scanResult.scannedRoots == 0);
  REQUIRE(scanResult.scannedPaths == 2);
  REQUIRE(scanResult.changedEntries == 2);

  const auto entries = syncRepo.getEntriesForSyncRoot(root.rootId);
  REQUIRE(entries.size() == 2);

  bool foundDeletedOld = false;
  bool foundNew = false;
  for (const auto &entry : entries) {
    if (entry.localPath == "old.txt") {
      REQUIRE(entry.syncState == "deleted");
      foundDeletedOld = true;
    }
    if (entry.localPath == "new.txt") {
      REQUIRE(entry.syncState == "pending_upload");
      foundNew = true;
    }
  }

  REQUIRE(foundDeletedOld);
  REQUIRE(foundNew);
}
