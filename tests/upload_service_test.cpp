#include "crypto/Aad.hpp"
#include "crypto/RustCrypto.hpp"
#include "fs/FileEncryptor.hpp"
#include "helpers/Base64.hpp"
#include "repo/UploadResumeRepo.hpp"
#include "repo/UserRepo.hpp"
#include "api/HttpError.hpp"
#include "service/UploadService.hpp"
#include "service/VaultService.hpp"

#include "support/FakeSecureStorage.hpp"
#include "support/NullArkiveHttpClient.hpp"
#include "support/TestDatabase.hpp"
#include "support/TestFs.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace {

class FakeArkiveApi final : public ArkiveApi {
public:
  FakeArkiveApi() : ArkiveApi(client_) {}

  void failPutCall(int putCall, int statusCode, std::string body) {
    failPutCall_ = putCall;
    failStatusCode_ = statusCode;
    failBody_ = std::move(body);
  }

  void clearPutFailure() { failPutCall_.reset(); }

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
                               .providerUploadId = request.singlePart ? ""
                                                                     : "provider-1",
                               .fileChunkSize = request.fileChunkSize,
                               .totalChunks = request.totalChunks,
                               .uploadPartSize = request.uploadPartSize,
                               .uploadPartCount = request.uploadPartCount,
                               .uploadUrl = request.singlePart
                                                ? "https://example.invalid/single"
                                                : ""};
  }

  std::string presignSingleUpload(const std::string &uploadSessionId) override {
    ++singlePresignCalls;
    lastPresignSession = uploadSessionId;
    return "https://example.invalid/single";
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
                            const std::vector<uint8_t> &body) override {
    ++putCalls;
    lastPutUrl = presignedUrl;
    lastPutBody = body;
    if (failPutCall_.has_value() && putCalls == *failPutCall_) {
      throw HttpError(failStatusCode_, failBody_);
    }
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
  int singlePresignCalls = 0;
  int putCalls = 0;
  int uploadPartCalls = 0;
  int completeCalls = 0;

  std::optional<StartUploadRequest> lastStartUpload;
  std::optional<std::string> lastPresignSession;
  std::vector<int> lastPresignParts;
  std::optional<std::string> lastPutUrl;
  std::vector<uint8_t> lastPutBody;
  std::optional<std::string> lastUploadPartSession;
  std::optional<UploadPartRequest> lastUploadPart;
  std::optional<std::string> lastCompleteSession;
  std::optional<UploadCompleteRequest> lastComplete;
  std::optional<int> failPutCall_;
  int failStatusCode_ = 503;
  std::string failBody_;

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

bool bytesContainAsciiSubstring(const std::vector<uint8_t> &bytes,
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

TEST_CASE("UploadService uses single PUT for one-chunk files") {
  TestDatabase db;
  TempDir tempDir;
  RustCrypto crypto;

  UserRepo userRepo(db.get());
  VaultService vaultService(userRepo, crypto,
                            std::make_unique<FakeSecureStorage>());
  seedUnlockedAccount(userRepo, vaultService, crypto);

  FakeArkiveApi api;
  FileEncryptor encryptor(crypto, vaultService);
  UploadResumeRepo uploadResumeRepo(db.get());
  UploadService uploadService(api, encryptor, uploadResumeRepo);

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
  REQUIRE(api.lastStartUpload->singlePart);
  REQUIRE(api.presignCalls == 0);
  REQUIRE(api.singlePresignCalls == 0);
  REQUIRE(api.putCalls >= 1);
  REQUIRE(api.uploadPartCalls == 0);
  REQUIRE(api.completeCalls == 1);

  REQUIRE(api.lastCompleteSession.has_value());
  REQUIRE(*api.lastCompleteSession == "session-1");

  // Safety check: encrypted PUT body should not contain plaintext.
  REQUIRE_FALSE(bytesContainAsciiSubstring(api.lastPutBody, plaintext));
}

TEST_CASE("UploadService resumes persisted multipart upload after retryable failure") {
  TestDatabase db;
  TempDir tempDir;
  RustCrypto crypto;

  UserRepo userRepo(db.get());
  VaultService vaultService(userRepo, crypto,
                            std::make_unique<FakeSecureStorage>());
  seedUnlockedAccount(userRepo, vaultService, crypto);

  FakeArkiveApi api;
  FileEncryptor encryptor(crypto, vaultService);
  UploadResumeRepo uploadResumeRepo(db.get());
  UploadService uploadService(api, encryptor, uploadResumeRepo);

  const auto filePath = tempDir.path() / "movie.bin";
  const std::string plaintext(9 * 1024 * 1024, 'x');
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
      .localHash = std::string("hash-1"),
      .remoteUpdatedAt = std::nullopt,
      .syncState = "pending_upload",
      .lastSyncedAt = std::nullopt,
  };

  api.failPutCall(2, 503, R"({"error":"temporary failure"})");
  REQUIRE_THROWS_AS(uploadService.uploadFile(filePath, entry), HttpError);

  const auto persistedSession =
      uploadResumeRepo.getSessionByLocalPath(std::filesystem::absolute(filePath)
                                                 .lexically_normal()
                                                 .string());
  REQUIRE(persistedSession.has_value());
  const auto persistedParts =
      uploadResumeRepo.listParts(persistedSession->uploadSessionId);
  REQUIRE(persistedParts.size() == 1);
  REQUIRE(persistedParts.front().partNumber == 1);

  api.clearPutFailure();
  const UploadFileResponse resumed = uploadService.uploadFile(filePath, entry);

  REQUIRE(resumed.uploadSessionId == "session-1");
  REQUIRE(api.startUploadCalls == 1);
  REQUIRE(api.putCalls == 3);
  REQUIRE(api.uploadPartCalls == 2);
  REQUIRE(api.completeCalls == 1);
  REQUIRE_FALSE(
      uploadResumeRepo
          .getSessionByLocalPath(std::filesystem::absolute(filePath)
                                     .lexically_normal()
                                     .string())
          .has_value());
}
