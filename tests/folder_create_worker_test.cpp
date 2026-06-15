#include "api/ArkiveApi.hpp"
#include "crypto/Aad.hpp"
#include "crypto/RustCrypto.hpp"
#include "fs/FileEncryptor.hpp"
#include "helpers/Base64.hpp"
#include "repo/EntryRepo.hpp"
#include "repo/SyncRepo.hpp"
#include "repo/UserRepo.hpp"
#include "service/FolderCreateWorker.hpp"
#include "service/VaultService.hpp"

#include "support/FakeSecureStorage.hpp"
#include "support/NullArkiveHttpClient.hpp"
#include "support/TestDatabase.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

namespace {

class FakeArkiveApi final : public ArkiveApi {
public:
  FakeArkiveApi() : ArkiveApi(client_) {}

  CreateFolderResponse createFolder(const CreateFolderRequest &request) override {
    requests.push_back(request);
    return CreateFolderResponse{
        .id = "remote-folder-1",
        .parentFolderId = request.parentFolderId,
        .encryptedName = request.encryptedName,
        .encryptedMetadata = request.encryptedMetadata,
    };
  }

  std::vector<CreateFolderRequest> requests;

private:
  NullArkiveHttpClient client_;
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
      .userId = std::string("user-1"),
      .email = std::string("test@example.com"),
      .vaultSalt = encodeBase64(salt),
      .encryptedMasterKey = encodeBase64(encryptedMasterKey),
      .vaultSessionKeyId = std::nullopt,
      .vaultSessionBlob = std::nullopt,
  });

  vaultService.unlock(password);
}

} // namespace

TEST_CASE("FolderCreateWorker creates remote folder and updates entry") {
  TestDatabase db;
  RustCrypto crypto;
  UserRepo userRepo(db.get());
  VaultService vaultService(userRepo, crypto,
                            std::make_unique<FakeSecureStorage>());
  seedUnlockedAccount(userRepo, vaultService, crypto);

  SyncRepo syncRepo(db.get());
  EntryRepo entryRepo(db.get());
  FileEncryptor fileEncryptor(crypto, vaultService);
  FakeArkiveApi api;

  syncRepo.upsertSyncRoot({
      .Id = "root-1",
      .localPath = "/tmp/root-1",
      .folderId = "root-folder-1",
      .enabled = true,
  });
  entryRepo.upsertDirectoryEntry({
      .syncRootId = "root-1",
      .relativePath = "docs",
      .lastSeenScanId = "scan-1",
  });

  const auto entry = entryRepo.findEntryByPath("root-1", "docs");
  REQUIRE(entry.has_value());

  FolderCreateWorker worker(syncRepo, entryRepo, userRepo, fileEncryptor, api);
  worker.run(TransferJob{
      .id = "job-1",
      .entryId = entry->id,
      .jobType = "create_folder",
      .status = "running",
      .localPath = "docs",
      .remoteId = std::nullopt,
      .remoteFolderId = std::optional<std::string>("root-folder-1"),
      .bytesTotal = 0,
      .bytesDone = 0,
      .retryCount = 0,
  });

  REQUIRE(api.requests.size() == 1);
  REQUIRE(api.requests[0].parentFolderId ==
          std::optional<std::string>("root-folder-1"));
  REQUIRE(api.requests[0].encryptedName != "");
  REQUIRE(api.requests[0].encryptedMetadata != "");

  const std::vector<uint8_t> decryptedMetadata = crypto.decryptChunk(
      vaultService.masterKey(),
      ArkiveAad::toBytes(ArkiveAad::kFolderMetadata),
      decodeBase64(api.requests[0].encryptedMetadata));
  const auto metadataJson =
      nlohmann::json::parse(std::string(decryptedMetadata.begin(),
                                        decryptedMetadata.end()));
  REQUIRE(metadataJson.at("name").get<std::string>() == "docs");

  const auto updated = entryRepo.getEntryById(entry->id);
  REQUIRE(updated.has_value());
  REQUIRE(updated->remoteId == std::optional<std::string>("remote-folder-1"));
  REQUIRE(updated->parentFolderId ==
          std::optional<std::string>("root-folder-1"));
  REQUIRE(updated->syncState == EntrySyncState::Unchanged);
}

TEST_CASE("FolderCreateWorker creates remote sync root folder when missing") {
  TestDatabase db;
  RustCrypto crypto;
  UserRepo userRepo(db.get());
  VaultService vaultService(userRepo, crypto,
                            std::make_unique<FakeSecureStorage>());
  seedUnlockedAccount(userRepo, vaultService, crypto);

  SyncRepo syncRepo(db.get());
  EntryRepo entryRepo(db.get());
  FileEncryptor fileEncryptor(crypto, vaultService);
  FakeArkiveApi api;

  syncRepo.upsertSyncRoot({
      .Id = "root-1",
      .localPath = "/tmp/daemon-test",
      .folderId = "",
      .enabled = true,
  });

  FolderCreateWorker worker(syncRepo, entryRepo, userRepo, fileEncryptor, api);
  REQUIRE(worker.ensureRootFolder("root-1"));

  REQUIRE(api.requests.size() == 1);
  REQUIRE_FALSE(api.requests[0].parentFolderId.has_value());

  const std::vector<uint8_t> decryptedMetadata = crypto.decryptChunk(
      vaultService.masterKey(),
      ArkiveAad::toBytes(ArkiveAad::kFolderMetadata),
      decodeBase64(api.requests[0].encryptedMetadata));
  const auto metadataJson =
      nlohmann::json::parse(std::string(decryptedMetadata.begin(),
                                        decryptedMetadata.end()));
  REQUIRE(metadataJson.at("name").get<std::string>() == "daemon-test");

  const auto updatedRoot = syncRepo.findSyncRootById("root-1");
  REQUIRE(updatedRoot.has_value());
  REQUIRE(updatedRoot->folderId == "remote-folder-1");
}
