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
    if (responses.empty()) {
      return {};
    }

    const ListSyncEntriesResponse response = responses.front();
    responses.erase(responses.begin());
    return response;
  }

  std::vector<ListSyncEntriesRequest> requests;
  std::vector<ListSyncEntriesResponse> responses;

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
  api.responses = {
      ListSyncEntriesResponse{
          .entries = {
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
          },
          .nextCursor = std::string("cursor-1"),
          .hasMore = true,
      },
      ListSyncEntriesResponse{
          .entries = {
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
          },
          .nextCursor = std::nullopt,
          .hasMore = false,
      },
      ListSyncEntriesResponse{
          .entries = {
              SyncEntryResponse{
                  .type = "file",
                  .id = "file-2",
                  .folderId = std::string("folder-2"),
                  .parentFolderId = std::string("folder-2"),
                  .encryptedMetadata = std::string("meta-child"),
                  .encryptedFileKey = std::string("key-child"),
                  .encryptedManifest = std::string("manifest-child"),
                  .encryptedName = std::nullopt,
                  .updatedAt = "2026-06-06T00:00:01Z",
                  .deletedAt = std::nullopt,
                  .purgedAt = std::nullopt,
              },
          },
          .nextCursor = std::nullopt,
          .hasMore = false,
      },
      ListSyncEntriesResponse{
          .entries = {
              SyncEntryResponse{
                  .type = "file",
                  .id = "file-3",
                  .folderId = std::nullopt,
                  .parentFolderId = std::nullopt,
                  .encryptedMetadata = std::string("meta3"),
                  .encryptedFileKey = std::string("key3"),
                  .encryptedManifest = std::string("manifest3"),
                  .encryptedName = std::nullopt,
                  .updatedAt = "2026-06-06T00:00:02Z",
                  .deletedAt = std::nullopt,
                  .purgedAt = std::nullopt,
              },
          },
          .nextCursor = std::nullopt,
          .hasMore = false,
      },
  };

  RemoteScanner scanner(syncRepo, api);
  scanner.scanAllRootsAndStore();

  const auto rootOneEntries = syncRepo.getEntriesForSyncRoot("root-1");
  const auto rootTwoEntries = syncRepo.getEntriesForSyncRoot("root-2");

  REQUIRE(rootOneEntries.size() == 3);
  REQUIRE(rootTwoEntries.size() == 1);
  REQUIRE(api.requests.size() == 4);
  REQUIRE(api.requests[0].folderId == std::optional<std::string>("folder-1"));
  REQUIRE(api.requests[0].includeDeleted);
  REQUIRE(api.requests[0].limit == 100);
  REQUIRE(api.requests[0].cursor == std::nullopt);
  REQUIRE(api.requests[1].folderId == std::optional<std::string>("folder-1"));
  REQUIRE(api.requests[1].includeDeleted);
  REQUIRE(api.requests[1].cursor == std::optional<std::string>("cursor-1"));
  REQUIRE(api.requests[2].includeDeleted);
  REQUIRE(api.requests[2].cursor == std::nullopt);
  REQUIRE(api.requests[2].folderId == std::optional<std::string>("folder-2"));
  REQUIRE(api.requests[3].folderId == std::nullopt);
  REQUIRE(api.requests[3].includeDeleted);
  REQUIRE(api.requests[3].cursor == std::nullopt);

  bool sawChildFolder = false;
  bool sawChildFile = false;
  for (const auto &entry : rootOneEntries) {
    if (entry.remoteFolderId == std::optional<std::string>("folder-2")) {
      sawChildFolder = true;
      REQUIRE(entry.remoteType == "folder");
      REQUIRE(entry.syncState == "synced");
    }
    if (entry.remoteFileId == std::optional<std::string>("file-2")) {
      sawChildFile = true;
      REQUIRE(entry.remoteParentFolderId == std::optional<std::string>("folder-2"));
      REQUIRE(entry.syncState == "synced");
    }
  }

  REQUIRE(sawChildFolder);
  REQUIRE(sawChildFile);
}

TEST_CASE("RemoteScanner updates existing SQLite entry by remote id") {
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

  syncRepo.upsertEntries({EntryRecord{
      .id = "local-entry-1",
      .remoteId = std::string("file-1"),
      .remoteFileId = std::string("file-1"),
      .remoteFolderId = std::nullopt,
      .syncRootId = "root-1",
      .remoteType = "file",
      .localPath = "movie.txt",
      .isDirectory = false,
      .parentFolderId = std::nullopt,
      .remoteParentFolderId = std::string("folder-1"),
      .encryptedName = std::string("old-name"),
      .localSize = 11,
      .localMtime = std::string("123"),
      .localHash = std::string("hash-1"),
      .remoteUpdatedAt = std::string("2026-06-01T00:00:00Z"),
      .remoteDeletedAt = std::nullopt,
      .remotePurgedAt = std::nullopt,
      .lastRemoteSeenAt = std::string("2026-06-01T00:00:00Z"),
      .syncState = "synced",
      .lastSyncedAt = std::nullopt,
  }});

  FakeArkiveApi api;
  api.responses = {
      ListSyncEntriesResponse{
          .entries = {
              SyncEntryResponse{
                  .type = "file",
                  .id = "file-1",
                  .folderId = std::string("folder-1"),
                  .parentFolderId = std::string("folder-1"),
                  .encryptedMetadata = std::string("meta"),
                  .encryptedFileKey = std::string("key"),
                  .encryptedManifest = std::string("manifest"),
                  .encryptedName = std::string("new-name"),
                  .updatedAt = "2026-06-06T00:00:00Z",
                  .deletedAt = std::nullopt,
                  .purgedAt = std::nullopt,
              },
          },
          .nextCursor = std::nullopt,
          .hasMore = false,
      },
  };

  RemoteScanner scanner(syncRepo, api);
  scanner.scanAllRootsAndStore();

  const auto updated = syncRepo.getEntryById("local-entry-1");
  REQUIRE(updated.has_value());
  REQUIRE(updated->localPath == "movie.txt");
  REQUIRE(updated->remoteFileId == std::optional<std::string>("file-1"));
  REQUIRE(updated->encryptedName == std::optional<std::string>("new-name"));
  REQUIRE(updated->remoteUpdatedAt ==
          std::optional<std::string>("2026-06-06T00:00:00Z"));
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
