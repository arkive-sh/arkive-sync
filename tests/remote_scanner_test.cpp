#include "api/ArkiveApi.hpp"
#include "crypto/Aad.hpp"
#include "crypto/RustCrypto.hpp"
#include "helpers/Base64.hpp"
#include "helpers/LocalPathProtector.hpp"
#include "repo/SyncRepo.hpp"
#include "repo/UserRepo.hpp"
#include "sync/Reconcile.hpp"
#include "sync/RemoteScanner.hpp"
#include "service/VaultService.hpp"

#include "support/FakeSecureStorage.hpp"
#include "support/NullArkiveHttpClient.hpp"
#include "support/TestDatabase.hpp"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <vector>

namespace {

class FakeArkiveApi final : public ArkiveApi {
public:
  FakeArkiveApi() : ArkiveApi(client_) {}

  ListSyncEntriesResponse
  listSyncEntries(const ListSyncEntriesRequest &request) override {
    requests.push_back(request);
    return nextResponse;
  }

  std::vector<ListSyncEntriesRequest> requests;
  ListSyncEntriesResponse nextResponse;

private:
  NullArkiveHttpClient client_;
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

} // namespace

TEST_CASE("RemoteScanner scans enabled roots with include_deleted") {
  TestDatabase db;
  RustCrypto crypto;
  UserRepo userRepo(db.get());
  VaultService vaultService(userRepo, crypto,
                            std::make_unique<FakeSecureStorage>());
  seedUnlockedAccount(userRepo, vaultService, crypto);
  LocalPathProtector pathProtector(crypto, vaultService);
  SyncRepo syncRepo(db.get(), pathProtector);

  syncRepo.upsertSyncRoot(SyncRootRecord{
      .id = "root-1",
      .localPath = "/tmp/root-1",
      .folderId = std::string("folder-1"),
      .enabled = true,
  });
  syncRepo.upsertSyncRoot(SyncRootRecord{
      .id = "root-2",
      .localPath = "/tmp/root-2",
      .folderId = std::nullopt,
      .enabled = true,
  });
  syncRepo.upsertSyncRoot(SyncRootRecord{
      .id = "root-3",
      .localPath = "/tmp/root-3",
      .folderId = std::string("folder-3"),
      .enabled = false,
  });

  FakeArkiveApi api;
  api.nextResponse.entries = {
      SyncEntryResponse{
          .type = "file",
          .id = "file-1",
          .folderId = std::string("folder-1"),
          .parentFolderId = std::nullopt,
          .encryptedMetadata = std::string("meta"),
          .encryptedFileKey = std::string("key"),
          .encryptedManifest = std::string("manifest"),
          .encryptedName = std::nullopt,
          .updatedAt = "2026-06-06T00:00:00Z",
          .deletedAt = std::nullopt,
          .purgedAt = std::nullopt,
      },
      SyncEntryResponse{
          .type = "folder",
          .id = "folder-2",
          .folderId = std::nullopt,
          .parentFolderId = std::string("folder-1"),
          .encryptedMetadata = std::string("meta2"),
          .encryptedFileKey = std::nullopt,
          .encryptedManifest = std::nullopt,
          .encryptedName = std::string("name"),
          .updatedAt = "2026-06-06T00:00:01Z",
          .deletedAt = std::nullopt,
          .purgedAt = std::nullopt,
      },
  };

  RemoteScanner scanner(syncRepo, api);
  const RemoteScanResult result = scanner.scanAllRoots();

  REQUIRE(result.roots.size() == 2);
  REQUIRE(result.totalEntryCount() == 4);
  REQUIRE(api.requests.size() == 2);
  REQUIRE(api.requests[0].folderId == std::optional<std::string>("folder-1"));
  REQUIRE(api.requests[0].includeDeleted);
  REQUIRE(api.requests[1].folderId == std::nullopt);
  REQUIRE(api.requests[1].includeDeleted);
}

TEST_CASE("Reconcile exposes sync mode and returns placeholder decisions") {
  Reconcile reconcile(SyncMode::RemoteMirror);

  ListSyncEntriesResponse response{
      .entries = {
          SyncEntryResponse{
              .type = "file",
              .id = "file-1",
              .folderId = std::nullopt,
              .parentFolderId = std::nullopt,
              .encryptedMetadata = std::nullopt,
              .encryptedFileKey = std::nullopt,
              .encryptedManifest = std::nullopt,
              .encryptedName = std::nullopt,
              .updatedAt = "2026-06-06T00:00:00Z",
              .deletedAt = std::nullopt,
              .purgedAt = std::nullopt,
          },
      },
  };

  REQUIRE(reconcile.mode() == SyncMode::RemoteMirror);
  REQUIRE(reconcile.spec().direction == SyncModeDirection::RemoteToLocal);

  const auto decisions = reconcile.decide(response);
  REQUIRE(decisions.size() == 1);
  REQUIRE(decisions.front().entryId == "file-1");
  REQUIRE(decisions.front().action == ReconcileAction::Noop);
}
