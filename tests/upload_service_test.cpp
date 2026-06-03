#include "crypto/Aad.hpp"
#include "crypto/RustCrypto.hpp"
#include "fs/FileEncryptor.hpp"
#include "helpers/Base64.hpp"
#include "repo/UserRepo.hpp"
#include "service/UploadService.hpp"
#include "service/VaultService.hpp"

#include "support/FakeSecureStorage.hpp"
#include "support/NullArkiveHttpClient.hpp"
#include "support/TestDatabase.hpp"
#include "support/TestFs.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace {

class FakeArkiveApi final : public ArkiveApi {
public:
  FakeArkiveApi() : ArkiveApi(client_) {}

  UploadLimitsResponse uploadLimits() override {
    ++uploadLimitsCalls;
    return UploadLimitsResponse{.maxQueueItems = 100,
                                .partConcurrency = 1,
                                .staleUploadHours = 24};
  }

  StartUploadResponse startUpload(const StartUploadRequest &request) override {
    ++startUploadCalls;
    lastStartUpload = request;
    return StartUploadResponse{.fileId = "file-1",
                               .vaultId = "vault-1",
                               .uploadSessionId = "session-1",
                               .providerUploadId = "provider-1",
                               .fileChunkSize = request.fileChunkSize,
                               .totalChunks = request.totalChunks,
                               .uploadPartSize = request.uploadPartSize,
                               .uploadPartCount = request.uploadPartCount};
  }

  PresignPartsResponse presignParts(const std::string &uploadSessionId,
                                    const std::vector<int> &partNumbers) override {
    ++presignCalls;
    lastPresignSession = uploadSessionId;
    lastPresignParts = partNumbers;
    PresignPartsResponse response;
    for (int part : partNumbers) {
      response.urls.emplace(part, "https://example.invalid/presigned/" +
                                     std::to_string(part));
    }
    return response;
  }

  std::string
  putEncryptedPartToStorage(const std::string &presignedUrl,
                            const std::vector<std::byte> &body) override {
    ++putCalls;
    lastPutUrl = presignedUrl;
    lastPutBody = body;
    return "etag-1";
  }

  void uploadPart(const std::string &uploadSessionId,
                  const UploadPartRequest &request) override {
    ++uploadPartCalls;
    lastUploadPartSession = uploadSessionId;
    lastUploadPart = request;
  }

  void uploadComplete(const std::string &uploadSessionId,
                      const UploadCompleteRequest &request) override {
    ++completeCalls;
    lastCompleteSession = uploadSessionId;
    lastComplete = request;
  }

  int uploadLimitsCalls = 0;
  int startUploadCalls = 0;
  int presignCalls = 0;
  int putCalls = 0;
  int uploadPartCalls = 0;
  int completeCalls = 0;

  std::optional<StartUploadRequest> lastStartUpload;
  std::optional<std::string> lastPresignSession;
  std::vector<int> lastPresignParts;
  std::optional<std::string> lastPutUrl;
  std::vector<std::byte> lastPutBody;
  std::optional<std::string> lastUploadPartSession;
  std::optional<UploadPartRequest> lastUploadPart;
  std::optional<std::string> lastCompleteSession;
  std::optional<UploadCompleteRequest> lastComplete;

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

bool bytesContainAsciiSubstring(const std::vector<std::byte> &bytes,
                                const std::string &needle) {
  if (needle.empty() || bytes.empty() || needle.size() > bytes.size()) {
    return false;
  }
  for (size_t i = 0; i + needle.size() <= bytes.size(); ++i) {
    bool match = true;
    for (size_t j = 0; j < needle.size(); ++j) {
      if (static_cast<unsigned char>(bytes[i + j]) !=
          static_cast<unsigned char>(needle[j])) {
        match = false;
        break;
      }
    }
    if (match) {
      return true;
    }
  }
  return false;
}

} // namespace

TEST_CASE("UploadService happy path calls start/presign/put/part/complete") {
  TestDatabase db;
  TempDir tempDir;
  RustCrypto crypto;

  UserRepo userRepo(db.get());
  VaultService vaultService(userRepo, crypto,
                            std::make_unique<FakeSecureStorage>());
  seedUnlockedAccount(userRepo, vaultService, crypto);

  FakeArkiveApi api;
  FileEncryptor encryptor(crypto, vaultService);
  UploadService uploadService(api, encryptor);

  const auto filePath = tempDir.path() / "movie.txt";
  const std::string plaintext = "hello";
  writeFile(filePath, plaintext);

  const EntryRecord entry{
      .id = "entry-1",
      .remoteId = std::nullopt,
      .syncRootId = "root-1",
      .remoteType = "file",
      .localPath = filePath.filename().string(),
      .isDirectory = false,
      .parentFolderId = std::nullopt,
      .encryptedName = std::nullopt,
      .localSize = static_cast<int64_t>(std::filesystem::file_size(filePath)),
      .localMtime = std::nullopt,
      .localHash = std::nullopt,
      .remoteUpdatedAt = std::nullopt,
      .syncState = "pending_upload",
      .lastSyncedAt = std::nullopt,
  };

  const UploadFileResponse response = uploadService.uploadFile(filePath, entry);

  REQUIRE(response.fileId == "file-1");

  REQUIRE(api.startUploadCalls == 1);
  REQUIRE(api.presignCalls >= 1);
  REQUIRE(api.putCalls >= 1);
  REQUIRE(api.uploadPartCalls >= 1);
  REQUIRE(api.completeCalls == 1);

  REQUIRE(api.lastPresignSession.has_value());
  REQUIRE(*api.lastPresignSession == "session-1");
  REQUIRE(api.lastUploadPartSession.has_value());
  REQUIRE(*api.lastUploadPartSession == "session-1");
  REQUIRE(api.lastCompleteSession.has_value());
  REQUIRE(*api.lastCompleteSession == "session-1");

  // Safety check: encrypted PUT body should not contain plaintext.
  REQUIRE_FALSE(bytesContainAsciiSubstring(api.lastPutBody, plaintext));
}
