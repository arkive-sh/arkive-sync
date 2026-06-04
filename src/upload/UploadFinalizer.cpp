#include "upload/UploadFinalizer.hpp"

#include "crypto/Aad.hpp"
#include "fs/FileEncryptor.hpp"
#include "helpers/Base64.hpp"
#include "helpers/Mime.hpp"
#include <nlohmann/json.hpp>

namespace {

void appendBytes(std::vector<uint8_t> &target,
                 const std::vector<uint8_t> &source) {
  target.insert(target.end(), source.begin(), source.end());
}

nlohmann::json buildMetadata(const std::filesystem::path &path,
                             const EntryRecord &entry) {
  const FileMimeDetails details = describeFileMime(path);
  return {
      {"schema", "arkive.file.metadata"},
      {"version", 1},
      {"name", details.name},
      {"mime", details.mime},
      {"extension", details.extension},
      {"size", entry.localSize.value_or(0)},
      {"preview", nullptr},
  };
}

} // namespace

UploadFinalizer::UploadFinalizer(ArkiveApi &api, FileEncryptor &fileEncryptor)
    : api_(api), fileEncryptor_(fileEncryptor) {}

UploadArtifacts UploadFinalizer::completeUpload(
    const std::filesystem::path &path, const EntryRecord &entry,
    const StartUploadResponse &started, const UploadPlan &plan,
    const std::vector<UploadedPartResult> &parts,
    const std::vector<uint8_t> &fileKey) {
  UploadArtifacts artifacts;
  std::vector<uint8_t> combinedChunkHashes;
  nlohmann::json manifestChunks = nlohmann::json::array();

  try {
    for (const auto &part : parts) {
      for (const auto &chunk : part.chunks) {
        manifestChunks.push_back({
            {"n", chunk.chunkNumber},
            {"plain_size", chunk.plaintextSize},
            {"cipher_size", chunk.ciphertextSize},
            {"hash", chunk.encryptedHash},
        });
      }
      appendBytes(combinedChunkHashes, part.combinedChunkHashes);
    }

    const FileMimeDetails details = describeFileMime(path);
    artifacts.fileKey = fileKey;
    artifacts.encryptedMetadata = fileEncryptor_.encryptMetadata(
        buildMetadata(path, entry).dump(), artifacts.fileKey, started.vaultId,
        started.fileId);

    const nlohmann::json manifest = {
        {"schema", "arkive.file.manifest"},
        {"version", 1},
        {"hash_alg", "blake3"},
        {"hash_encoding", "base64"},
        {"file_id", started.fileId},
        {"name", details.name},
        {"mime", details.mime},
        {"extension", details.extension},
        {"size", plan.originalSize},
        {"chunk_size", static_cast<uint64_t>(started.fileChunkSize)},
        {"chunks", manifestChunks},
    };
    const std::string manifestJson = manifest.dump();
    artifacts.encryptedManifest = fileEncryptor_.encryptChunk(
        std::vector<uint8_t>(manifestJson.begin(), manifestJson.end()),
        artifacts.fileKey,
        ArkiveAad::toBytes(
            ArkiveAad::makeFileManifest(started.vaultId, started.fileId)));

    artifacts.encryptedHash = fileEncryptor_.hashBytes(combinedChunkHashes);
    artifacts.encryptedFileKey =
        fileEncryptor_.wrapFileKey(artifacts.fileKey, started.vaultId,
                                   started.fileId);

    api_.uploadComplete(
        started.uploadSessionId,
        UploadCompleteRequest{
            .encryptedMetadata = encodeBase64(artifacts.encryptedMetadata),
            .encryptedFileKey = encodeBase64(artifacts.encryptedFileKey),
            .encryptedManifest = encodeBase64(artifacts.encryptedManifest),
            .encryptedHash = encodeBase64(artifacts.encryptedHash),
            .searchTokens = fileEncryptor_.createSearchTokenEntries(
                started.vaultId, details.name, details.mime),
            .hasThumbnail = false,
            .thumbnailMime = "",
            .thumbnailWidth = 0,
            .thumbnailHeight = 0,
        });
  } catch (...) {
    if (!artifacts.fileKey.empty()) {
      fileEncryptor_.zeroize(artifacts.fileKey);
    }
    if (!artifacts.encryptedFileKey.empty()) {
      fileEncryptor_.zeroize(artifacts.encryptedFileKey);
    }
    if (!artifacts.encryptedMetadata.empty()) {
      fileEncryptor_.zeroize(artifacts.encryptedMetadata);
    }
    if (!artifacts.encryptedManifest.empty()) {
      fileEncryptor_.zeroize(artifacts.encryptedManifest);
    }
    if (!artifacts.encryptedHash.empty()) {
      fileEncryptor_.zeroize(artifacts.encryptedHash);
    }
    if (!combinedChunkHashes.empty()) {
      fileEncryptor_.zeroize(combinedChunkHashes);
    }
    throw;
  }

  if (!combinedChunkHashes.empty()) {
    fileEncryptor_.zeroize(combinedChunkHashes);
  }
  return artifacts;
}
