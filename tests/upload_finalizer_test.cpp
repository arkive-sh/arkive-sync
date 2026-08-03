#include "crypto/Aad.hpp"
#include "crypto/RustCrypto.hpp"
#include "fs/FileEncryptor.hpp"
#include "helpers/Base64.hpp"
#include "repo/UserRepo.hpp"
#include "service/VaultService.hpp"
#include "upload/ThumbnailGenerator.hpp"
#include "upload/UploadFinalizer.hpp"

#include "support/FakeSecureStorage.hpp"
#include "support/NullArkiveHttpClient.hpp"
#include "support/TestDatabase.hpp"
#include "support/TestFs.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

class FakeArkiveApi final : public ArkiveApi {
public:
  FakeArkiveApi() : ArkiveApi(client_) {}

  std::string presignThumbnail(const std::string &uploadSessionId,
                               const PresignThumbnailRequest &request) override {
    ++presignThumbnailCalls;
    lastPresignThumbnailSession = uploadSessionId;
    lastPresignThumbnail = request;
    return "https://example.invalid/thumbnail";
  }

  std::string
  putEncryptedPartToStorage(const std::string &presignedUrl,
                            const std::vector<uint8_t> &body) override {
    ++putCalls;
    lastPutUrl = presignedUrl;
    lastPutBody = body;
    return "etag-thumbnail";
  }

  void uploadComplete(const std::string &uploadSessionId,
                      const UploadCompleteRequest &request) override {
    ++completeCalls;
    lastCompleteSession = uploadSessionId;
    lastComplete = request;
  }

  int presignThumbnailCalls = 0;
  int putCalls = 0;
  int completeCalls = 0;
  std::optional<std::string> lastPresignThumbnailSession;
  std::optional<PresignThumbnailRequest> lastPresignThumbnail;
  std::optional<std::string> lastPutUrl;
  std::vector<uint8_t> lastPutBody;
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

Entry testEntry(const std::filesystem::path &path) {
  return Entry{
      .id = "entry-1",
      .syncRootId = "root-1",
      .relativePath = path.filename().string(),
      .isDirectory = false,
      .size = static_cast<int64_t>(std::filesystem::file_size(path)),
      .syncState = EntrySyncState::PendingUpload,
  };
}

StartUploadResponse testStart() {
  return StartUploadResponse{
      .fileId = "file-1",
      .vaultId = "vault-1",
      .uploadSessionId = "session-1",
      .providerUploadId = "provider-1",
      .fileChunkSize = 8 * 1024 * 1024,
      .totalChunks = 1,
      .uploadPartSize = 8 * 1024 * 1024,
      .uploadPartCount = 1,
  };
}

UploadPlan testPlan() {
  return UploadPlan{
      .originalSize = 5,
      .fileChunkSize = 8 * 1024 * 1024,
      .totalChunks = 1,
      .uploadPartSize = 8 * 1024 * 1024,
      .chunksPerUploadPart = 1,
      .uploadPartCount = 1,
  };
}

std::vector<UploadedPartResult> testParts() {
  return {UploadedPartResult{
      .plan = UploadPartPlan{.partNumber = 1, .partStart = 0, .partEnd = 5,
                             .firstChunkNumber = 1},
      .chunks = {EncryptedChunkResult{
          .chunkNumber = 1,
          .plaintextSize = 5,
          .ciphertextSize = 46,
          .encryptedHash = "chunk-hash",
      }},
      .uploadHash = "upload-hash",
      .etag = "etag-1",
      .combinedChunkHashes = {1, 2, 3},
  }};
}

} // namespace

TEST_CASE("UploadFinalizer uploads thumbnail when generator returns one") {
  TestDatabase db;
  TempDir tempDir;
  RustCrypto crypto;
  UserRepo userRepo(db.get());
  VaultService vaultService(userRepo, crypto,
                            std::make_unique<FakeSecureStorage>());
  seedUnlockedAccount(userRepo, vaultService, crypto);

  const auto path = tempDir.path() / "photo.jpg";
  writeFile(path, "hello");

  FakeArkiveApi api;
  FileEncryptor encryptor(crypto, vaultService);
  UploadFinalizer finalizer(api, encryptor,
                            [](const std::filesystem::path &) {
                              return UploadThumbnail{
                                  .bytes = {1, 2, 3, 4},
                                  .mime = "image/webp",
                                  .width = 320,
                                  .height = 180,
                              };
                            });

  std::vector<uint8_t> fileKey = encryptor.createFileKey();
  UploadArtifacts artifacts = finalizer.completeUpload(
      path, testEntry(path), testStart(), testPlan(), testParts(), fileKey);

  REQUIRE(api.presignThumbnailCalls == 1);
  REQUIRE(api.putCalls == 1);
  REQUIRE(api.completeCalls == 1);
  REQUIRE(api.lastPresignThumbnailSession == std::optional<std::string>("session-1"));
  REQUIRE(api.lastPresignThumbnail->mime == "image/webp");
  REQUIRE(api.lastPresignThumbnail->width == 320);
  REQUIRE(api.lastPresignThumbnail->height == 180);
  REQUIRE(api.lastPresignThumbnail->encryptedSize ==
          static_cast<int64_t>(api.lastPutBody.size()));
  REQUIRE(api.lastComplete->hasThumbnail);
  REQUIRE(api.lastComplete->thumbnailMime == "image/webp");
  REQUIRE(api.lastComplete->thumbnailWidth == 320);
  REQUIRE(api.lastComplete->thumbnailHeight == 180);

  const std::vector<uint8_t> metadataBytes = crypto.decryptChunk(
      fileKey,
      ArkiveAad::toBytes(ArkiveAad::makeFileMetadata("vault-1", "file-1")),
      artifacts.encryptedMetadata);
  const auto metadata = nlohmann::json::parse(
      std::string(metadataBytes.begin(), metadataBytes.end()));
  REQUIRE(metadata.at("preview").at("has_thumbnail").get<bool>());
  REQUIRE(metadata.at("preview").at("thumbnail_mime").get<std::string>() ==
          "image/webp");
  REQUIRE(metadata.at("preview").at("thumbnail_width").get<int>() == 320);
  REQUIRE(metadata.at("preview").at("thumbnail_height").get<int>() == 180);
  REQUIRE(metadata.at("preview").at("thumbnail_size").get<int64_t>() ==
          api.lastPresignThumbnail->encryptedSize);

  encryptor.zeroize(fileKey);
  encryptor.zeroize(artifacts.fileKey);
}

TEST_CASE("UploadFinalizer completes upload when thumbnail generation fails") {
  TestDatabase db;
  TempDir tempDir;
  RustCrypto crypto;
  UserRepo userRepo(db.get());
  VaultService vaultService(userRepo, crypto,
                            std::make_unique<FakeSecureStorage>());
  seedUnlockedAccount(userRepo, vaultService, crypto);

  const auto path = tempDir.path() / "photo.jpg";
  writeFile(path, "hello");

  FakeArkiveApi api;
  FileEncryptor encryptor(crypto, vaultService);
  UploadFinalizer finalizer(api, encryptor,
                            [](const std::filesystem::path &)
                                -> std::optional<UploadThumbnail> {
                              throw std::runtime_error("boom");
                            });

  std::vector<uint8_t> fileKey = encryptor.createFileKey();
  UploadArtifacts artifacts = finalizer.completeUpload(
      path, testEntry(path), testStart(), testPlan(), testParts(), fileKey);

  REQUIRE(api.presignThumbnailCalls == 0);
  REQUIRE(api.putCalls == 0);
  REQUIRE(api.completeCalls == 1);
  REQUIRE_FALSE(api.lastComplete->hasThumbnail);

  const std::vector<uint8_t> metadataBytes = crypto.decryptChunk(
      fileKey,
      ArkiveAad::toBytes(ArkiveAad::makeFileMetadata("vault-1", "file-1")),
      artifacts.encryptedMetadata);
  const auto metadata = nlohmann::json::parse(
      std::string(metadataBytes.begin(), metadataBytes.end()));
  REQUIRE_FALSE(metadata.at("preview").at("has_thumbnail").get<bool>());

  encryptor.zeroize(fileKey);
  encryptor.zeroize(artifacts.fileKey);
}

TEST_CASE("ThumbnailGenerator creates WebP thumbnail for PNG when ffmpeg exists") {
  TempDir tempDir;
  const auto path = tempDir.path() / "grid.png";
  const unsigned char png[] = {
      0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00,
      0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
      0x00, 0x01, 0x08, 0x04, 0x00, 0x00, 0x00, 0xb5, 0x1c, 0x0c, 0x02,
      0x00, 0x00, 0x00, 0x0b, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63,
      0xfc, 0xff, 0x1f, 0x00, 0x03, 0x03, 0x02, 0x00, 0xef, 0xbf, 0xa7,
      0xdb, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42,
      0x60, 0x82,
  };
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  REQUIRE(stream.is_open());
  stream.write(reinterpret_cast<const char *>(png), sizeof(png));
  stream.close();

  const auto thumbnail = generateUploadThumbnail(path);
  if (!thumbnail.has_value()) {
    WARN("ffmpeg unavailable or lacks libwebp; skipping native generator check");
    return;
  }

  REQUIRE(thumbnail->mime == "image/webp");
  REQUIRE(thumbnail->width == 1);
  REQUIRE(thumbnail->height == 1);
  REQUIRE_FALSE(thumbnail->bytes.empty());
}
