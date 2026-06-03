#include "crypto/Aad.hpp"
#include "crypto/RustCrypto.hpp"
#include "db/SqliteHelpers.hpp"
#include "helpers/Base64.hpp"
#include "helpers/LocalPathProtector.hpp"
#include "repo/QueueRepo.hpp"
#include "repo/SyncRepo.hpp"
#include "repo/UserRepo.hpp"
#include "service/SyncService.hpp"
#include "service/VaultService.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sqlite3.h>
#include <unordered_map>

namespace {

class MemorySecureStorage : public SecureStorage {
public:
  void storeSecret(const std::string &service, const std::string &account,
                   const std::vector<uint8_t> &secret) override {
    secrets_[service + "\n" + account] = secret;
  }

  std::optional<std::vector<uint8_t>>
  loadSecret(const std::string &service,
             const std::string &account) override {
    const auto it = secrets_.find(service + "\n" + account);
    if (it == secrets_.end()) {
      return std::nullopt;
    }

    return it->second;
  }

  void deleteSecret(const std::string &service,
                    const std::string &account) override {
    secrets_.erase(service + "\n" + account);
  }

private:
  std::unordered_map<std::string, std::vector<uint8_t>> secrets_;
};

class TempDir {
public:
  TempDir() {
    path_ = std::filesystem::temp_directory_path() /
            ("arkive-sync-test-" +
             std::to_string(static_cast<long long>(
                 std::chrono::steady_clock::now().time_since_epoch().count())) +
             "-" + std::to_string(std::rand()));
    std::filesystem::create_directories(path_);
  }

  ~TempDir() { std::filesystem::remove_all(path_); }

  const std::filesystem::path &path() const { return path_; }

private:
  std::filesystem::path path_;
};

class TestDb {
public:
  TestDb() {
    REQUIRE(sqlite3_open(":memory:", &db_) == SQLITE_OK);
    execOrThrow(db_, R"sql(
CREATE TABLE account (
  id INTEGER PRIMARY KEY CHECK (id = 1),
  base_url TEXT NOT NULL,
  email TEXT,
  vault_salt TEXT,
  encrypted_master_key TEXT,
  vault_session_key_id TEXT,
  vault_session_blob TEXT,
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

INSERT INTO account (id, base_url) VALUES (1, 'http://localhost:8080');

CREATE TABLE sync_roots (
  id TEXT PRIMARY KEY,
  local_path TEXT NOT NULL UNIQUE,
  local_path_hash TEXT,
  folder_id TEXT,
  enabled INTEGER NOT NULL DEFAULT 1,
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE entries (
  id TEXT PRIMARY KEY,
  remote_id TEXT,
  sync_root_id TEXT NOT NULL,
  remote_type TEXT NOT NULL,
  local_path TEXT NOT NULL,
  local_path_hash TEXT,
  is_directory INTEGER NOT NULL DEFAULT 0,
  parent_folder_id TEXT,
  encrypted_name TEXT,
  local_size INTEGER,
  local_mtime TEXT,
  local_hash TEXT,
  remote_updated_at TEXT,
  sync_state TEXT NOT NULL,
  last_synced_at TEXT,
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE UNIQUE INDEX idx_entries_sync_root_local_path_hash
ON entries(sync_root_id, local_path_hash);

CREATE TABLE transfer_queue (
  id TEXT PRIMARY KEY,
  entry_id TEXT,
  direction TEXT NOT NULL,
  status TEXT NOT NULL,
  local_path TEXT NOT NULL,
  remote_id TEXT,
  folder_id TEXT,
  bytes_total INTEGER,
  bytes_done INTEGER NOT NULL DEFAULT 0,
  error_message TEXT,
  retry_count INTEGER NOT NULL DEFAULT 0,
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE UNIQUE INDEX idx_transfer_active_upload
ON transfer_queue(entry_id, direction)
WHERE direction = 'upload'
AND status IN ('queued', 'running');
)sql");
  }

  ~TestDb() {
    if (db_ != nullptr) {
      sqlite3_close(db_);
    }
  }

  sqlite3 *get() const { return db_; }

private:
  sqlite3 *db_ = nullptr;
};

void writeFile(const std::filesystem::path &path, const std::string &contents) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  REQUIRE(stream.is_open());
  stream << contents;
}

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

} // namespace

TEST_CASE("SyncService marks new file pending upload without queue row") {
  TestDb db;
  TempDir tempDir;
  RustCrypto crypto;
  UserRepo userRepo(db.get());
  VaultService vaultService(userRepo, crypto,
                            std::make_unique<MemorySecureStorage>());
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
                            std::make_unique<MemorySecureStorage>());
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
                            std::make_unique<MemorySecureStorage>());
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
                            std::make_unique<MemorySecureStorage>());
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

TEST_CASE("SyncService marks deleted file entry deleted") {
  TestDb db;
  TempDir tempDir;
  RustCrypto crypto;
  UserRepo userRepo(db.get());
  VaultService vaultService(userRepo, crypto,
                            std::make_unique<MemorySecureStorage>());
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
                            std::make_unique<MemorySecureStorage>());
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
                            std::make_unique<MemorySecureStorage>());
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
