#include "crypto/Aad.hpp"
#include "crypto/RustCrypto.hpp"
#include "fs/FileEncryptor.hpp"
#include "repo/EntryRepo.hpp"
#include "repo/LocalEntryRepo.hpp"
#include "repo/RemoteEntryRepo.hpp"
#include "repo/SyncRepo.hpp"
#include "repo/UserRepo.hpp"
#include "service/UploadJobRunner.hpp"
#include "service/UploadService.hpp"
#include "service/VaultService.hpp"

#include "helpers/Base64.hpp"
#include "support/FakeSecureStorage.hpp"
#include "support/TestDatabase.hpp"
#include "support/TestFs.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

namespace {

class FakeUploadService final : public IUploadService {
public:
  UploadFileResponse uploadFile(const std::filesystem::path &path,
                                const Entry &entry) override {
    lastPath = path;
    lastParentFolderId = entry.parentFolderId;
    return UploadFileResponse{
        .fileId = "remote-file-1",
        .vaultId = "vault-1",
        .uploadSessionId = "session-1",
        .providerUploadId = "provider-1",
    };
  }

  std::optional<std::filesystem::path> lastPath;
  std::optional<std::string> lastParentFolderId;
};

void seedUnlockedAccount(UserRepo &userRepo, VaultService &vaultService,
                         RustCrypto &crypto) {
  const std::string password = "test-password";
  const std::vector<uint8_t> salt = crypto.generateSalt();
  const std::vector<uint8_t> masterKey = crypto.generateMasterKey();
  const std::vector<uint8_t> encryptedMasterKey =
      crypto.wrapMasterKey(masterKey, crypto.derivePasswordKek(password, salt),
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

} // namespace

TEST_CASE("UploadJobRunner resolves parent folder remote id and saves remote_file_id") {
  TestDatabase db;
  TempDir tempDir;
  RustCrypto crypto;
  UserRepo userRepo(db.get());
  VaultService vaultService(userRepo, crypto,
                            std::make_unique<FakeSecureStorage>());
  seedUnlockedAccount(userRepo, vaultService, crypto);

  SyncRepo syncRepo(db.get());
  EntryRepo entryRepo(db.get());
  LocalEntryRepo localEntryRepo(db.get());
  RemoteEntryRepo remoteEntryRepo(db.get());

  syncRepo.upsertSyncRoot({
      .Id = "root-1",
      .localPath = tempDir.path().string(),
      .folderId = "root-folder-1",
      .enabled = 1,
  });

  localEntryRepo.upsertDirectoryEntry({
      .syncRootId = "root-1",
      .relativePath = "docs",
      .lastSeenScanId = "scan-1",
  });
  const auto parentEntry = entryRepo.findEntryByPath("root-1", "docs");
  REQUIRE(parentEntry.has_value());
  remoteEntryRepo.markFolderCreated(parentEntry->id, "remote-folder-2",
                                    std::optional<std::string>("root-folder-1"));

  const auto filePath = tempDir.path() / "docs" / "movie.txt";
  std::filesystem::create_directories(filePath.parent_path());
  writeFile(filePath, "hello");

  localEntryRepo.upsertFileEntry({
      .syncRootId = "root-1",
      .relativePath = "docs/movie.txt",
      .size = static_cast<int64_t>(std::filesystem::file_size(filePath)),
      .mtime = std::filesystem::last_write_time(filePath),
      .contentHash = "scan-hash-1",
      .syncState = EntrySyncState::PendingUpload,
      .lastSeenScanId = "scan-1",
  });
  const auto fileEntry = entryRepo.findEntryByPath("root-1", "docs/movie.txt");
  REQUIRE(fileEntry.has_value());

  FakeUploadService uploadService;
  UploadJobRunner runner(syncRepo, entryRepo, remoteEntryRepo, uploadService);
  runner.run(TransferJob{
      .id = "job-1",
      .entryId = fileEntry->id,
      .jobType = "upload_file",
      .status = "running",
      .localPath = "docs/movie.txt",
      .remoteId = std::nullopt,
      .remoteFolderId = std::optional<std::string>("remote-folder-2"),
      .bytesTotal = static_cast<uint64_t>(*fileEntry->size),
      .bytesDone = 0,
      .retryCount = 0,
  });

  REQUIRE(uploadService.lastPath == filePath);
  REQUIRE(uploadService.lastParentFolderId ==
          std::optional<std::string>("remote-folder-2"));

  const auto updated = entryRepo.getEntryById(fileEntry->id);
  REQUIRE(updated.has_value());
  REQUIRE(updated->remoteId == std::optional<std::string>("remote-file-1"));
  REQUIRE(updated->remoteFileId == std::optional<std::string>("remote-file-1"));
  REQUIRE(updated->parentFolderId ==
          std::optional<std::string>("remote-folder-2"));
  REQUIRE(updated->syncState == EntrySyncState::Unchanged);
}
