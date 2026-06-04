#include "crypto/RustCrypto.hpp"
#include "helpers/LocalPathProtector.hpp"
#include "repo/UserRepo.hpp"
#include "repo/SyncRepo.hpp"
#include "service/UploadJobRunner.hpp"
#include "service/UploadService.hpp"
#include "service/VaultService.hpp"

#include "crypto/Aad.hpp"
#include "helpers/Base64.hpp"

#include "support/FakeSecureStorage.hpp"
#include "support/TestDatabase.hpp"
#include "support/TestFs.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <stdexcept>

namespace {

class FakeUploadService final : public IUploadService {
public:
  void failNext(const std::string &message) {
    shouldFail_ = true;
    failMessage_ = message;
  }

  int callCount() const { return callCount_; }
  const std::optional<std::filesystem::path> &lastPath() const {
    return lastPath_;
  }
  const std::optional<std::string> &lastEntryId() const { return lastEntryId_; }

  UploadFileResponse uploadFile(const std::filesystem::path &path,
                                const EntryRecord &entry) override {
    ++callCount_;
    lastPath_ = path;
    lastEntryId_ = entry.id;
    if (shouldFail_) {
      throw std::runtime_error(failMessage_);
    }
    return UploadFileResponse{
        .fileId = "remote-file-1",
        .vaultId = "vault-1",
        .uploadSessionId = "session-1",
        .providerUploadId = "provider-1",
    };
  }

private:
  bool shouldFail_ = false;
  std::string failMessage_ = "boom";
  int callCount_ = 0;
  std::optional<std::filesystem::path> lastPath_;
  std::optional<std::string> lastEntryId_;
};

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

TEST_CASE("UploadJobRunner success uploads file, marks entry synced, saves remote_id") {
  TestDatabase db;
  TempDir tempDir;
  RustCrypto crypto;

  UserRepo userRepo(db.get());
  VaultService vaultService(userRepo, crypto,
                            std::make_unique<FakeSecureStorage>());
  seedUnlockedAccount(userRepo, vaultService, crypto);
  LocalPathProtector pathProtector(crypto, vaultService);
  SyncRepo syncRepo(db.get(), pathProtector);

  // Seed a sync root + one pending entry.
  const SyncRootRecord root{
      .id = "root-1",
      .localPath = tempDir.path().string(),
      .folderId = std::nullopt,
      .enabled = true,
  };
  syncRepo.upsertSyncRoot(root);

  const auto filePath = tempDir.path() / "movie.txt";
  writeFile(filePath, "hello");

  const EntryRecord entry{
      .id = "entry-1",
      .remoteId = std::nullopt,
      .syncRootId = root.id,
      .remoteType = "file",
      .localPath = "movie.txt",
      .isDirectory = false,
      .parentFolderId = std::nullopt,
      .encryptedName = std::nullopt,
      .localSize = static_cast<int64_t>(std::filesystem::file_size(filePath)),
      .localMtime = mtimeStringFor(filePath),
      .localHash = "scan-hash-1",
      .remoteUpdatedAt = std::nullopt,
      .syncState = "pending_upload",
      .lastSyncedAt = std::nullopt,
  };
  syncRepo.upsertEntries({entry});

  FakeUploadService fakeUpload;
  UploadJobRunner runner(syncRepo, fakeUpload);
  const TransferJob job{
      .id = "job-1",
      .entryId = entry.id,
      .direction = "upload",
      .status = "running",
      .localPath = entry.localPath,
      .remoteId = std::nullopt,
      .remoteFolderId = std::nullopt,
      .bytesTotal = static_cast<uint64_t>(*entry.localSize),
      .bytesDone = 0,
      .retryCount = 0,
  };

  runner.run(job);

  REQUIRE(fakeUpload.callCount() == 1);
  REQUIRE(fakeUpload.lastEntryId().has_value());
  REQUIRE(*fakeUpload.lastEntryId() == entry.id);

  const auto updated = syncRepo.getEntryById(entry.id);
  REQUIRE(updated.has_value());
  REQUIRE(updated->syncState == "synced");
  REQUIRE(updated->remoteId.has_value());
  REQUIRE(*updated->remoteId == "remote-file-1");
}

TEST_CASE("UploadJobRunner uploader failure throws; entry stays pending_upload") {
  TestDatabase db;
  TempDir tempDir;
  RustCrypto crypto;

  UserRepo userRepo(db.get());
  VaultService vaultService(userRepo, crypto,
                            std::make_unique<FakeSecureStorage>());
  seedUnlockedAccount(userRepo, vaultService, crypto);
  LocalPathProtector pathProtector(crypto, vaultService);
  SyncRepo syncRepo(db.get(), pathProtector);

  const SyncRootRecord root{
      .id = "root-1",
      .localPath = tempDir.path().string(),
      .folderId = std::nullopt,
      .enabled = true,
  };
  syncRepo.upsertSyncRoot(root);

  const auto filePath = tempDir.path() / "movie.txt";
  writeFile(filePath, "hello");
  const EntryRecord entry{
      .id = "entry-1",
      .remoteId = std::nullopt,
      .syncRootId = root.id,
      .remoteType = "file",
      .localPath = "movie.txt",
      .isDirectory = false,
      .parentFolderId = std::nullopt,
      .encryptedName = std::nullopt,
      .localSize = static_cast<int64_t>(std::filesystem::file_size(filePath)),
      .localMtime = mtimeStringFor(filePath),
      .localHash = "scan-hash-1",
      .remoteUpdatedAt = std::nullopt,
      .syncState = "pending_upload",
      .lastSyncedAt = std::nullopt,
  };
  syncRepo.upsertEntries({entry});

  FakeUploadService fakeUpload;
  fakeUpload.failNext("network boom");
  UploadJobRunner runner(syncRepo, fakeUpload);
  const TransferJob job{
      .id = "job-1",
      .entryId = entry.id,
      .direction = "upload",
      .status = "running",
      .localPath = entry.localPath,
      .remoteId = std::nullopt,
      .remoteFolderId = std::nullopt,
      .bytesTotal = static_cast<uint64_t>(*entry.localSize),
      .bytesDone = 0,
      .retryCount = 0,
  };

  REQUIRE_THROWS(runner.run(job));

  const auto updated = syncRepo.getEntryById(entry.id);
  REQUIRE(updated.has_value());
  REQUIRE(updated->syncState == "pending_upload");
  REQUIRE_FALSE(updated->remoteId.has_value());
}

TEST_CASE("UploadJobRunner missing local file throws") {
  TestDatabase db;
  TempDir tempDir;
  RustCrypto crypto;

  UserRepo userRepo(db.get());
  VaultService vaultService(userRepo, crypto,
                            std::make_unique<FakeSecureStorage>());
  seedUnlockedAccount(userRepo, vaultService, crypto);
  LocalPathProtector pathProtector(crypto, vaultService);
  SyncRepo syncRepo(db.get(), pathProtector);

  const SyncRootRecord root{
      .id = "root-1",
      .localPath = tempDir.path().string(),
      .folderId = std::nullopt,
      .enabled = true,
  };
  syncRepo.upsertSyncRoot(root);

  const EntryRecord entry{
      .id = "entry-1",
      .remoteId = std::nullopt,
      .syncRootId = root.id,
      .remoteType = "file",
      .localPath = "missing.txt",
      .isDirectory = false,
      .parentFolderId = std::nullopt,
      .encryptedName = std::nullopt,
      .localSize = 5,
      .localMtime = std::nullopt,
      .localHash = std::nullopt,
      .remoteUpdatedAt = std::nullopt,
      .syncState = "pending_upload",
      .lastSyncedAt = std::nullopt,
  };
  syncRepo.upsertEntries({entry});

  FakeUploadService fakeUpload;
  UploadJobRunner runner(syncRepo, fakeUpload);
  const TransferJob job{
      .id = "job-1",
      .entryId = entry.id,
      .direction = "upload",
      .status = "running",
      .localPath = entry.localPath,
      .remoteId = std::nullopt,
      .remoteFolderId = std::nullopt,
      .bytesTotal = 5,
      .bytesDone = 0,
      .retryCount = 0,
  };

  REQUIRE_THROWS(runner.run(job));
  REQUIRE(fakeUpload.callCount() == 0);
}

TEST_CASE("UploadJobRunner size mismatch before upload throws stale error") {
  TestDatabase db;
  TempDir tempDir;
  RustCrypto crypto;

  UserRepo userRepo(db.get());
  VaultService vaultService(userRepo, crypto,
                            std::make_unique<FakeSecureStorage>());
  seedUnlockedAccount(userRepo, vaultService, crypto);
  LocalPathProtector pathProtector(crypto, vaultService);
  SyncRepo syncRepo(db.get(), pathProtector);

  const SyncRootRecord root{
      .id = "root-1",
      .localPath = tempDir.path().string(),
      .folderId = std::nullopt,
      .enabled = true,
  };
  syncRepo.upsertSyncRoot(root);

  const auto filePath = tempDir.path() / "movie.txt";
  writeFile(filePath, "hello");
  const EntryRecord entry{
      .id = "entry-1",
      .remoteId = std::nullopt,
      .syncRootId = root.id,
      .remoteType = "file",
      .localPath = "movie.txt",
      .isDirectory = false,
      .parentFolderId = std::nullopt,
      .encryptedName = std::nullopt,
      .localSize = static_cast<int64_t>(std::filesystem::file_size(filePath)) + 1,
      .localMtime = mtimeStringFor(filePath),
      .localHash = "scan-hash-1",
      .remoteUpdatedAt = std::nullopt,
      .syncState = "pending_upload",
      .lastSyncedAt = std::nullopt,
  };
  syncRepo.upsertEntries({entry});

  FakeUploadService fakeUpload;
  UploadJobRunner runner(syncRepo, fakeUpload);
  const TransferJob job{
      .id = "job-1",
      .entryId = entry.id,
      .direction = "upload",
      .status = "running",
      .localPath = entry.localPath,
      .remoteId = std::nullopt,
      .remoteFolderId = std::nullopt,
      .bytesTotal = static_cast<uint64_t>(*entry.localSize),
      .bytesDone = 0,
      .retryCount = 0,
  };

  REQUIRE_THROWS_AS(runner.run(job), StaleUploadError);
  REQUIRE(fakeUpload.callCount() == 0);

  const auto updated = syncRepo.getEntryById(entry.id);
  REQUIRE(updated.has_value());
  REQUIRE(updated->syncState == "pending_upload");
}

TEST_CASE("UploadJobRunner mtime mismatch before upload throws stale error") {
  TestDatabase db;
  TempDir tempDir;
  RustCrypto crypto;

  UserRepo userRepo(db.get());
  VaultService vaultService(userRepo, crypto,
                            std::make_unique<FakeSecureStorage>());
  seedUnlockedAccount(userRepo, vaultService, crypto);
  LocalPathProtector pathProtector(crypto, vaultService);
  SyncRepo syncRepo(db.get(), pathProtector);

  const SyncRootRecord root{
      .id = "root-1",
      .localPath = tempDir.path().string(),
      .folderId = std::nullopt,
      .enabled = true,
  };
  syncRepo.upsertSyncRoot(root);

  const auto filePath = tempDir.path() / "movie.txt";
  writeFile(filePath, "hello");
  const std::string originalMtime = mtimeStringFor(filePath);
  std::filesystem::last_write_time(
      filePath, std::filesystem::last_write_time(filePath) + std::chrono::seconds(2));

  const EntryRecord entry{
      .id = "entry-1",
      .remoteId = std::nullopt,
      .syncRootId = root.id,
      .remoteType = "file",
      .localPath = "movie.txt",
      .isDirectory = false,
      .parentFolderId = std::nullopt,
      .encryptedName = std::nullopt,
      .localSize = static_cast<int64_t>(std::filesystem::file_size(filePath)),
      .localMtime = originalMtime,
      .localHash = "scan-hash-1",
      .remoteUpdatedAt = std::nullopt,
      .syncState = "pending_upload",
      .lastSyncedAt = std::nullopt,
  };
  syncRepo.upsertEntries({entry});

  FakeUploadService fakeUpload;
  UploadJobRunner runner(syncRepo, fakeUpload);
  const TransferJob job{
      .id = "job-1",
      .entryId = entry.id,
      .direction = "upload",
      .status = "running",
      .localPath = entry.localPath,
      .remoteId = std::nullopt,
      .remoteFolderId = std::nullopt,
      .bytesTotal = static_cast<uint64_t>(*entry.localSize),
      .bytesDone = 0,
      .retryCount = 0,
  };

  REQUIRE_THROWS_AS(runner.run(job), StaleUploadError);
  REQUIRE(fakeUpload.callCount() == 0);

  const auto updated = syncRepo.getEntryById(entry.id);
  REQUIRE(updated.has_value());
  REQUIRE(updated->syncState == "pending_upload");
}
