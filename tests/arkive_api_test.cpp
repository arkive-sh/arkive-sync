#include "api/ArkiveApi.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

class FakeHttpClient final : public ArkiveHttpClient {
public:
  FakeHttpClient() : ArkiveHttpClient("http://example.invalid", "") {}

  nlohmann::json getJson(const std::string &path) override {
    lastPath = path;
    return nextJson;
  }

  std::string lastPath;
  nlohmann::json nextJson;
};

} // namespace

TEST_CASE("ArkiveApi parses sync download crypto fields") {
  FakeHttpClient client;
  client.nextJson = {
      {"entries",
       {{{"type", "file"},
         {"id", "file-1"},
         {"folder_id", "folder-1"},
         {"encrypted_metadata", "metadata-b64"},
         {"encrypted_file_key", "key-b64"},
         {"encrypted_manifest", "manifest-b64"},
         {"updated_at", "2026-08-02T00:00:00Z"}}}},
      {"has_more", false},
  };

  ArkiveApi api(client);
  const auto response = api.listSyncEntries(std::nullopt, true);

  REQUIRE(client.lastPath == "/api/sync/entries?include_deleted=true&limit=100");
  REQUIRE(response.entries.size() == 1);
  REQUIRE(response.entries.front().remoteFileId ==
          std::optional<std::string>("file-1"));
  REQUIRE(response.entries.front().encryptedFileKey ==
          std::optional<std::string>("key-b64"));
  REQUIRE(response.entries.front().encryptedManifest ==
          std::optional<std::string>("manifest-b64"));
}

TEST_CASE("ArkiveApi parses file record") {
  FakeHttpClient client;
  client.nextJson = {
      {"fileId", "file-1"},
      {"vaultId", "vault-1"},
      {"encryptionVersion", 1},
      {"chunkSize", 4194304},
      {"totalChunks", 2},
      {"plaintextSize", 5},
      {"encryptedHash", "hash-b64"},
      {"encryptedMetadata", "metadata-b64"},
      {"encryptedFileKey", "key-b64"},
      {"encryptedManifest", "manifest-b64"},
      {"sourceUrl", "https://storage.invalid/object"},
  };

  ArkiveApi api(client);
  const auto record = api.getFileRecord("file-1");

  REQUIRE(client.lastPath == "/api/files/file-1/record");
  REQUIRE(record.fileId == "file-1");
  REQUIRE(record.vaultId == "vault-1");
  REQUIRE(record.chunkSize == 4194304);
  REQUIRE(record.totalChunks == 2);
  REQUIRE(record.encryptedFileKey == "key-b64");
  REQUIRE(record.encryptedManifest == "manifest-b64");
  REQUIRE(record.sourceUrl == "https://storage.invalid/object");
}
