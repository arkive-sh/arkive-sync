#include "crypto/Aad.hpp"
#include "crypto/RustCrypto.hpp"
#include "helpers/Base64.hpp"
#include "helpers/LocalPathProtector.hpp"
#include "repo/QueueRepo.hpp"
#include "repo/SyncRepo.hpp"
#include "repo/UserRepo.hpp"
#include "service/SyncService.hpp"
#include "service/VaultService.hpp"

#include "support/FakeSecureStorage.hpp"
#include "support/TestDatabase.hpp"
#include "support/TestFs.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>

#include <optional>
#include <string>
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

EntryRecord onlyEntryForRoot(SyncRepo &syncRepo, const std::string &syncRootId) {
  const auto entries = syncRepo.getEntriesForSyncRoot(syncRootId);
  REQUIRE(entries.size() == 1);
  return entries.front();
}

std::string mtimeStringFor(const std::filesystem::path &path) {
  const auto mtime = std::filesystem::last_write_time(path);
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      mtime.time_since_epoch())
                      .count();
  return std::to_string(static_cast<long long>(ms));
}

} // namespace

TEST_CASE("SyncService marks new file pending upload without queue row") {
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
  SyncService syncService(syncRepo, queueRepo, crypto);

  const auto filePath = tempDir.path() / "movie.txt";
  writeFile(filePath, "hello");

  syncService.scanRoot(tempDir.path());

  const auto roots = syncRepo.getSyncRoots();
  REQUIRE(roots.size() == 1);
  const EntryRecord entry = onlyEntryForRoot(syncRepo, roots.front().id);
  REQUIRE(entry.localPath == "movie.txt");
  REQUIRE(entry.syncState == "pending_upload");
  REQUIRE(queueRepo.stats().queued == 0);
}

TEST_CASE("SyncService second unchanged scan creates no duplicate entry or queue row") {
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
  SyncService syncService(syncRepo, queueRepo, crypto);

  const auto filePath = tempDir.path() / "movie.txt";
  writeFile(filePath, "hello");

  syncService.scanRoot(tempDir.path());
  const auto roots = syncRepo.getSyncRoots();
  REQUIRE(roots.size() == 1);
  const EntryRecord firstEntry = onlyEntryForRoot(syncRepo, roots.front().id);
  syncRepo.markEntrySynced(firstEntry.id);

  syncService.scanRoot(tempDir.path());

  const auto entries = syncRepo.getEntriesForSyncRoot(roots.front().id);
  REQUIRE(entries.size() == 1);
  REQUIRE(entries.front().id == firstEntry.id);
  REQUIRE(entries.front().syncState == "synced");
  REQUIRE(queueRepo.stats().queued == 0);
}

TEST_CASE("SyncService second unchanged scan keeps pending upload as one entry") {
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
  SyncService syncService(syncRepo, queueRepo, crypto);

  const auto filePath = tempDir.path() / "movie.txt";
  writeFile(filePath, "hello");

  syncService.scanRoot(tempDir.path());
  const auto roots = syncRepo.getSyncRoots();
  REQUIRE(roots.size() == 1);

  syncService.scanRoot(tempDir.path());

  const auto entries = syncRepo.getEntriesForSyncRoot(roots.front().id);
  REQUIRE(entries.size() == 1);
  REQUIRE(entries.front().localPath == "movie.txt");
  REQUIRE(entries.front().syncState == "pending_upload");
  REQUIRE(queueRepo.stats().queued == 0);
}

TEST_CASE("SyncService marks changed file pending upload") {
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
  SyncService syncService(syncRepo, queueRepo, crypto);

  const auto filePath = tempDir.path() / "movie.txt";
  writeFile(filePath, "hello");
  syncService.scanRoot(tempDir.path());

  const auto roots = syncRepo.getSyncRoots();
  REQUIRE(roots.size() == 1);
  const EntryRecord firstEntry = onlyEntryForRoot(syncRepo, roots.front().id);
  syncRepo.markEntrySynced(firstEntry.id);

  writeFile(filePath, "hello changed");
  syncService.scanRoot(tempDir.path());

  const EntryRecord changedEntry = onlyEntryForRoot(syncRepo, roots.front().id);
  REQUIRE(changedEntry.id == firstEntry.id);
  REQUIRE(changedEntry.syncState == "pending_upload");
  REQUIRE(changedEntry.localHash != firstEntry.localHash);
}

TEST_CASE("SyncService updates metadata only when mtime changes but hash stays same") {
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
  SyncService syncService(syncRepo, queueRepo, crypto);

  const auto filePath = tempDir.path() / "movie.txt";
  writeFile(filePath, "hello");
  syncService.scanRoot(tempDir.path());

  const auto roots = syncRepo.getSyncRoots();
  REQUIRE(roots.size() == 1);
  const EntryRecord firstEntry = onlyEntryForRoot(syncRepo, roots.front().id);
  syncRepo.markEntrySynced(firstEntry.id);

  std::filesystem::last_write_time(
      filePath, std::filesystem::last_write_time(filePath) + std::chrono::seconds(2));
  const std::string updatedMtime = mtimeStringFor(filePath);

  syncService.scanRoot(tempDir.path());

  const EntryRecord rescannedEntry = onlyEntryForRoot(syncRepo, roots.front().id);
  REQUIRE(rescannedEntry.id == firstEntry.id);
  REQUIRE(rescannedEntry.syncState == "synced");
  REQUIRE(rescannedEntry.localHash == firstEntry.localHash);
  REQUIRE(rescannedEntry.localMtime == updatedMtime);
  REQUIRE(rescannedEntry.localMtime != firstEntry.localMtime);
}

TEST_CASE("SyncService marks deleted file entry deleted") {
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
  SyncService syncService(syncRepo, queueRepo, crypto);

  const auto filePath = tempDir.path() / "movie.txt";
  writeFile(filePath, "hello");
  syncService.scanRoot(tempDir.path());

  const auto roots = syncRepo.getSyncRoots();
  REQUIRE(roots.size() == 1);
  const EntryRecord firstEntry = onlyEntryForRoot(syncRepo, roots.front().id);
  syncRepo.markEntrySynced(firstEntry.id);

  std::filesystem::remove(filePath);
  syncService.scanRoot(tempDir.path());

  const EntryRecord deletedEntry = onlyEntryForRoot(syncRepo, roots.front().id);
  REQUIRE(deletedEntry.id == firstEntry.id);
  REQUIRE(deletedEntry.syncState == "deleted");
}

TEST_CASE("SyncService marks multiple files pending upload") {
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
  SyncService syncService(syncRepo, queueRepo, crypto);

  writeFile(tempDir.path() / "movie.txt", "movie");
  writeFile(tempDir.path() / "music.mp3", "music");
  writeFile(tempDir.path() / "photo.jpg", "photo");

  syncService.scanRoot(tempDir.path());

  const auto roots = syncRepo.getSyncRoots();
  REQUIRE(roots.size() == 1);
  const auto entries = syncRepo.getEntriesForSyncRoot(roots.front().id);
  REQUIRE(entries.size() == 3);
  for (const auto &entry : entries) {
    REQUIRE(entry.syncState == "pending_upload");
  }
}

TEST_CASE("SyncService stores nested file paths as portable relative paths") {
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
  SyncService syncService(syncRepo, queueRepo, crypto);

  std::filesystem::create_directories(tempDir.path() / "docs");
  std::filesystem::create_directories(tempDir.path() / "images");
  writeFile(tempDir.path() / "docs" / "report.pdf", "report");
  writeFile(tempDir.path() / "images" / "photo.jpg", "photo");

  syncService.scanRoot(tempDir.path());

  const auto roots = syncRepo.getSyncRoots();
  REQUIRE(roots.size() == 1);
  const auto entries = syncRepo.getEntriesForSyncRoot(roots.front().id);
  REQUIRE(entries.size() == 4);

  bool sawDocsDir = false;
  bool sawDocsFile = false;
  bool sawImagesDir = false;
  bool sawImagesFile = false;
  for (const auto &entry : entries) {
    if (entry.localPath == "docs") {
      sawDocsDir = true;
    }
    if (entry.localPath == "docs/report.pdf") {
      sawDocsFile = true;
    }
    if (entry.localPath == "images") {
      sawImagesDir = true;
    }
    if (entry.localPath == "images/photo.jpg") {
      sawImagesFile = true;
    }
  }

  REQUIRE(sawDocsDir);
  REQUIRE(sawDocsFile);
  REQUIRE(sawImagesDir);
  REQUIRE(sawImagesFile);
}
