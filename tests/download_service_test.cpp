#include "download/DownloadService.hpp"

#include "crypto/Aad.hpp"
#include "helpers/Base64.hpp"
#include "repo/UserRepo.hpp"
#include "service/VaultService.hpp"

#include "support/FakeSecureStorage.hpp"
#include "support/TestDatabase.hpp"
#include "support/TestFs.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <nlohmann/json.hpp>

namespace {

class FakeHttpClient final : public ArkiveHttpClient {
public:
  FakeHttpClient() : ArkiveHttpClient("http://example.invalid", "") {}

  void getRangeToSink(const std::string &pathOrUrl, uint64_t offset,
                      uint64_t length, const ByteSink &sink) override {
    REQUIRE(pathOrUrl == "https://storage.invalid/file-1");
    REQUIRE(offset + length <= ciphertext.size());
    sink(ciphertext.data() + offset, static_cast<std::size_t>(length));
  }

  std::vector<uint8_t> ciphertext;
};

class FakeArkiveApi final : public ArkiveApi {
public:
  explicit FakeArkiveApi(ArkiveHttpClient &client) : ArkiveApi(client) {}

  FileRecordResponse getFileRecord(const std::string &fileId) override {
    REQUIRE(fileId == record.fileId);
    return record;
  }

  FileRecordResponse record;
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

std::string readFile(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  REQUIRE(input.is_open());
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

} // namespace

TEST_CASE("DownloadService writes decrypted remote file") {
  TestDatabase db;
  TempDir tempDir;
  RustCrypto crypto;
  UserRepo userRepo(db.get());
  VaultService vaultService(userRepo, crypto,
                            std::make_unique<FakeSecureStorage>());
  seedUnlockedAccount(userRepo, vaultService, crypto);

  const std::string fileId = "file-1";
  const std::string vaultId = "vault-1";
  const int64_t chunkSize = 4;
  const int totalChunks = 2;
  const std::vector<uint8_t> fileKey = crypto.generateFileKey();
  const std::vector<uint8_t> firstPlain{'h', 'e', 'l', 'l'};
  const std::vector<uint8_t> secondPlain{'o'};
  const auto firstEncrypted = crypto.encryptChunk(
      fileKey,
      ArkiveAad::toBytes(
          ArkiveAad::makeFileChunk(vaultId, fileId, 1, chunkSize, totalChunks)),
      firstPlain);
  const auto secondEncrypted = crypto.encryptChunk(
      fileKey,
      ArkiveAad::toBytes(
          ArkiveAad::makeFileChunk(vaultId, fileId, 2, chunkSize, totalChunks)),
      secondPlain);

  FakeHttpClient http;
  http.ciphertext.insert(http.ciphertext.end(), firstEncrypted.begin(),
                         firstEncrypted.end());
  http.ciphertext.insert(http.ciphertext.end(), secondEncrypted.begin(),
                         secondEncrypted.end());

  const nlohmann::json metadata = {
      {"schema", "arkive.file.metadata"},
      {"version", 1},
      {"name", "movie.txt"},
      {"mime", "text/plain"},
      {"size", 5},
  };
  const nlohmann::json manifest = {
      {"schema", "arkive.file.manifest"},
      {"version", 1},
      {"file_id", fileId},
      {"name", "movie.txt"},
      {"mime", "text/plain"},
      {"size", 5},
      {"chunk_size", chunkSize},
      {"chunks",
       {{{"n", 1},
         {"plain_size", firstPlain.size()},
         {"cipher_size", firstEncrypted.size()}},
        {{"n", 2},
         {"plain_size", secondPlain.size()},
         {"cipher_size", secondEncrypted.size()}}}},
  };
  const std::string metadataJson = metadata.dump();
  const std::string manifestJson = manifest.dump();

  FakeArkiveApi api(http);
  api.record = FileRecordResponse{
      .fileId = fileId,
      .vaultId = vaultId,
      .encryptionVersion = 1,
      .chunkSize = chunkSize,
      .totalChunks = totalChunks,
      .plaintextSize = 5,
      .encryptedMetadata = encodeBase64(crypto.encryptChunk(
          fileKey,
          ArkiveAad::toBytes(ArkiveAad::makeFileMetadata(vaultId, fileId)),
          std::vector<uint8_t>(metadataJson.begin(), metadataJson.end()))),
      .encryptedFileKey = encodeBase64(crypto.wrapFileKey(
          fileKey, vaultService.masterKey(),
          ArkiveAad::toBytes(ArkiveAad::makeFileKey(vaultId, fileId)))),
      .encryptedManifest = encodeBase64(crypto.encryptChunk(
          fileKey,
          ArkiveAad::toBytes(ArkiveAad::makeFileManifest(vaultId, fileId)),
          std::vector<uint8_t>(manifestJson.begin(), manifestJson.end()))),
      .sourceUrl = "https://storage.invalid/file-1",
  };

  DownloadRecordDecryptor decryptor(crypto, vaultService);
  DownloadService service(api, http, crypto, decryptor);
  const auto target = tempDir.path() / "nested" / "movie.txt";

  service.downloadFile(fileId, target);

  REQUIRE(readFile(target) == "hello");
}
