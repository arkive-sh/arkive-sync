#include "upload/UploadFinalizer.hpp"

#include "crypto/Aad.hpp"
#include "fs/FileEncryptor.hpp"
#include "helpers/Base64.hpp"
#include "helpers/Mime.hpp"
#include <cstddef>
#include <nlohmann/json.hpp>
#include <optional>
#include <spdlog/spdlog.h>
#include <utility>

namespace {

constexpr size_t kMaxEncryptedThumbnailBytes = 150 * 1024;

void appendBytes(std::vector<uint8_t> &target,
                 const std::vector<uint8_t> &source) {
  target.insert(target.end(), source.begin(), source.end());
}

nlohmann::json buildMetadata(const std::filesystem::path &path,
                             const Entry &entry,
                             const nlohmann::json &preview) {
  const FileMimeDetails details = describeFileMime(path);
  return {
      {"schema", "arkive.file.metadata"},
      {"version", 1},
      {"name", details.name},
      {"mime", details.mime},
      {"extension", details.extension},
      {"size", entry.size.value_or(0)},
      {"preview", preview},
  };
}

} // namespace

UploadFinalizer::UploadFinalizer(ArkiveApi &api, FileEncryptor &fileEncryptor)
    : UploadFinalizer(api, fileEncryptor, generateUploadThumbnail) {}

UploadFinalizer::UploadFinalizer(ArkiveApi &api, FileEncryptor &fileEncryptor,
                                 ThumbnailGenerator thumbnailGenerator)
    : api_(api), fileEncryptor_(fileEncryptor),
      thumbnailGenerator_(std::move(thumbnailGenerator)) {}

UploadArtifacts UploadFinalizer::completeUpload(
    const std::filesystem::path &path, const Entry &entry,
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

    nlohmann::json previewMetadata = {
        {"thumbnail_file_id", nullptr},
        {"has_thumbnail", false},
    };
    bool hasThumbnail = false;
    std::string thumbnailMime;
    int thumbnailWidth = 0;
    int thumbnailHeight = 0;

    std::optional<UploadThumbnail> thumbnail;
    std::vector<uint8_t> encryptedThumbnail;
    try {
      thumbnail = thumbnailGenerator_(path);
      if (thumbnail.has_value()) {
        encryptedThumbnail = fileEncryptor_.encryptChunk(
            thumbnail->bytes, artifacts.fileKey,
            ArkiveAad::toBytes(ArkiveAad::makeFileThumbnail(started.vaultId,
                                                            started.fileId)));
        if (encryptedThumbnail.size() <= kMaxEncryptedThumbnailBytes) {
          const std::string url =
              api_.presignThumbnail(started.uploadSessionId,
                                    PresignThumbnailRequest{
                                        .encryptedSize = static_cast<int64_t>(
                                            encryptedThumbnail.size()),
                                        .mime = thumbnail->mime,
                                        .width = thumbnail->width,
                                        .height = thumbnail->height,
                                    });
          api_.putEncryptedPartToStorage(url, encryptedThumbnail);
          hasThumbnail = true;
          thumbnailMime = thumbnail->mime;
          thumbnailWidth = thumbnail->width;
          thumbnailHeight = thumbnail->height;
          previewMetadata = {
              {"thumbnail_file_id", nullptr},
              {"has_thumbnail", true},
              {"thumbnail_mime", thumbnailMime},
              {"thumbnail_width", thumbnailWidth},
              {"thumbnail_height", thumbnailHeight},
              {"thumbnail_size", static_cast<int64_t>(encryptedThumbnail.size())},
              {"thumbnail_version", 1},
          };
          spdlog::info("Uploaded thumbnail for {} size={} width={} height={}",
                       path.string(), encryptedThumbnail.size(),
                       thumbnailWidth, thumbnailHeight);
        } else {
          spdlog::warn("Skipping thumbnail for {}: encrypted size {} exceeds {}",
                       path.string(), encryptedThumbnail.size(),
                       kMaxEncryptedThumbnailBytes);
        }
      }
    } catch (const std::exception &error) {
      spdlog::warn("Skipping thumbnail for {}: {}", path.string(),
                   error.what());
    }
    if (!encryptedThumbnail.empty()) {
      fileEncryptor_.zeroize(encryptedThumbnail);
    }
    if (thumbnail.has_value() && !thumbnail->bytes.empty()) {
      fileEncryptor_.zeroize(thumbnail->bytes);
    }

    artifacts.encryptedMetadata = fileEncryptor_.encryptMetadata(
        buildMetadata(path, entry, previewMetadata).dump(), artifacts.fileKey,
        started.vaultId, started.fileId);

    api_.uploadComplete(
        started.uploadSessionId,
        UploadCompleteRequest{
            .encryptedMetadata = encodeBase64(artifacts.encryptedMetadata),
            .encryptedFileKey = encodeBase64(artifacts.encryptedFileKey),
            .encryptedManifest = encodeBase64(artifacts.encryptedManifest),
            .encryptedHash = encodeBase64(artifacts.encryptedHash),
            .searchTokens = fileEncryptor_.createSearchTokenEntries(
                started.vaultId, details.name),
            .hasThumbnail = hasThumbnail,
            .thumbnailMime = thumbnailMime,
            .thumbnailWidth = thumbnailWidth,
            .thumbnailHeight = thumbnailHeight,
            .thumbnailSize = static_cast<int64_t>(encryptedThumbnail.size()),
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
