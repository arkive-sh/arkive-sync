#include "api/ArkiveApi.hpp"
#include "crypto/Aad.hpp"
#include "crypto/RustCrypto.hpp"
#include "helpers/Base64.hpp"
#include "helpers/LocalPathProtector.hpp"
#include "repo/QueueRepo.hpp"
#include "repo/SyncRepo.hpp"
#include "repo/UserRepo.hpp"
#include "service/QueueService.hpp"
#include "service/SyncService.hpp"
#include "service/UploadJobRunner.hpp"
#include "service/UploadService.hpp"
#include "service/VaultService.hpp"

#include "support/FakeSecureStorage.hpp"
#include "support/NullArkiveHttpClient.hpp"
#include "support/TestDatabase.hpp"
#include "support/TestFs.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <optional>
#include <vector>

namespace {

class FakeArkiveApi final : public ArkiveApi {
public:
  FakeArkiveApi() : ArkiveApi(client_) {}

  UploadLimitsResponse uploadLimits() override {
    return UploadLimitsResponse{
        .maxQueueItems = 10,
        .partConcurrency = 1,
        .staleUploadHours = 24,
    };
  }

private:
  NullArkiveHttpClient client_;
};

class FakeUploadService final : public IUploadService {
public:
  UploadFileResponse uploadFile(const std::filesystem::path &path,
                                const EntryRecord &entry) override {
    ++callCount_;
    lastPath_ = path;
    lastEntryId_ = entry.id;
    return UploadFileResponse{
        .fileId = "remote-file-1",
        .vaultId = "vault-1",
        .uploadSessionId = "session-1",
        .providerUploadId = "provider-1",
    };
  }

  int callCount() const { return callCount_; }
  const std::optional<std::filesystem::path> &lastPath() const {
    return lastPath_;
  }

private:
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

std::string mtimeStringFor(const std::filesystem::path &path) {
  const auto mtime = std::filesystem::last_write_time(path);
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      mtime.time_since_epoch())
                      .count();
  return std::to_string(static_cast<long long>(ms));
}

} // namespace

TEST_CASE("QueueService retries stale upload after rescan and then uploads refreshed entry") {
  TestDatabase db;
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
  FakeUploadService uploadService;
  UploadJobRunner uploadJobRunner(syncRepo, uploadService);
  FakeArkiveApi api;
  QueueService queueService(queueRepo, syncRepo, uploadJobRunner, syncService,
                            api);

  const auto filePath = tempDir.path() / "movie.txt";
  writeFile(filePath, "hello");
  syncService.scanRoot(tempDir.path());

  const auto roots = syncRepo.roots().getSyncRoots();
  REQUIRE(roots.size() == 1);
  const auto entries = syncRepo.local().getEntriesForSyncRoot(roots.front().id);
  REQUIRE(entries.size() == 1);

  EntryRecord staleEntry = entries.front();
  staleEntry.localSize = *staleEntry.localSize + 1;
  staleEntry.localMtime = std::to_string(std::stoll(*staleEntry.localMtime) - 1);
  syncRepo.local().upsertEntries({staleEntry});

  queueRepo.enqueueUpload(staleEntry.id, staleEntry.localPath,
                          staleEntry.parentFolderId,
                          static_cast<uint64_t>(staleEntry.localSize.value_or(0)));

  queueService.processQueuedUploads();

  REQUIRE(uploadService.callCount() == 1);

  const auto updatedEntry = syncRepo.local().getEntryById(staleEntry.id);
  REQUIRE(updatedEntry.has_value());
  REQUIRE(updatedEntry->syncState == "synced");
  REQUIRE(updatedEntry->remoteId == std::optional<std::string>("remote-file-1"));
  REQUIRE(updatedEntry->localSize ==
          std::optional<int64_t>(static_cast<int64_t>(std::filesystem::file_size(filePath))));
  REQUIRE(updatedEntry->localMtime == mtimeStringFor(filePath));

  const QueueStats stats = queueRepo.stats();
  REQUIRE(stats.queued == 0);
  REQUIRE(stats.running == 0);
  REQUIRE(stats.failed == 0);
  REQUIRE(stats.done == 1);
}
