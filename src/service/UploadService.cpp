#include "service/UploadService.hpp"

#include "crypto/Aad.hpp"
#include "fs/ArkiveFileReader.hpp"
#include "fs/FileHasher.hpp"
#include <filesystem>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <vector>

namespace {

constexpr uint64_t kMultipartUploadPartSize = 8ull * 1024ull * 1024ull;

constexpr char kBase64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string encodeBase64(const std::vector<uint8_t> &bytes) {
  std::string encoded;
  encoded.reserve(((bytes.size() + 2) / 3) * 4);

  std::size_t index = 0;
  while (index + 3 <= bytes.size()) {
    const uint32_t block = (static_cast<uint32_t>(bytes[index]) << 16) |
                           (static_cast<uint32_t>(bytes[index + 1]) << 8) |
                           static_cast<uint32_t>(bytes[index + 2]);
    encoded.push_back(kBase64Alphabet[(block >> 18) & 0x3F]);
    encoded.push_back(kBase64Alphabet[(block >> 12) & 0x3F]);
    encoded.push_back(kBase64Alphabet[(block >> 6) & 0x3F]);
    encoded.push_back(kBase64Alphabet[block & 0x3F]);
    index += 3;
  }

  const std::size_t remainder = bytes.size() - index;
  if (remainder == 1) {
    const uint32_t block = static_cast<uint32_t>(bytes[index]) << 16;
    encoded.push_back(kBase64Alphabet[(block >> 18) & 0x3F]);
    encoded.push_back(kBase64Alphabet[(block >> 12) & 0x3F]);
    encoded.push_back('=');
    encoded.push_back('=');
  } else if (remainder == 2) {
    const uint32_t block = (static_cast<uint32_t>(bytes[index]) << 16) |
                           (static_cast<uint32_t>(bytes[index + 1]) << 8);
    encoded.push_back(kBase64Alphabet[(block >> 18) & 0x3F]);
    encoded.push_back(kBase64Alphabet[(block >> 12) & 0x3F]);
    encoded.push_back(kBase64Alphabet[(block >> 6) & 0x3F]);
    encoded.push_back('=');
  }

  return encoded;
}

std::vector<std::byte> toByteVector(const std::vector<uint8_t> &bytes) {
  std::vector<std::byte> converted;
  converted.reserve(bytes.size());
  for (const uint8_t byte : bytes) {
    converted.push_back(static_cast<std::byte>(byte));
  }
  return converted;
}

void appendBytes(std::vector<uint8_t> &target,
                 const std::vector<uint8_t> &source) {
  target.insert(target.end(), source.begin(), source.end());
}

std::string detectMimeType(const std::filesystem::path &path) {
  const std::string extension = path.extension().string();
  if (extension == ".jpg" || extension == ".jpeg") {
    return "image/jpeg";
  }
  if (extension == ".png") {
    return "image/png";
  }
  if (extension == ".gif") {
    return "image/gif";
  }
  if (extension == ".txt") {
    return "text/plain";
  }
  if (extension == ".json") {
    return "application/json";
  }

  return "application/octet-stream";
}

nlohmann::json buildMetadata(const std::filesystem::path &path,
                             const EntryRecord &entry) {
  return {
      {"schema", "arkive.file.metadata"},
      {"version", 1},
      {"name", path.filename().string()},
      {"mime", detectMimeType(path)},
      {"extension", path.has_extension() ? path.extension().string() : ""},
      {"size", entry.localSize.value_or(0)},
      {"preview", nullptr},
  };
}

} // namespace

UploadService::UploadService(ArkiveApi &api, FileEncryptor &fileEncryptor)
    : api_(api), fileEncryptor_(fileEncryptor) {}

UploadFileResponse UploadService::uploadFile(const std::filesystem::path &path,
                                             const EntryRecord &entry) {
  if (entry.isDirectory) {
    throw std::invalid_argument("uploadFile does not support directories");
  }
  if (!entry.localSize.has_value() || *entry.localSize < 0) {
    throw std::invalid_argument("uploadFile requires a valid local file size");
  }

  const uint64_t originalSize = static_cast<uint64_t>(*entry.localSize);
  const uint64_t fileChunkSize = static_cast<uint64_t>(kChunkSize);
  const uint64_t totalChunks =
      std::max<uint64_t>(1, (originalSize + fileChunkSize - 1) / fileChunkSize);
  const uint64_t uploadPartSize =
      totalChunks > 1 ? kMultipartUploadPartSize : fileChunkSize;
  const uint64_t chunksPerUploadPart =
      std::max<uint64_t>(1, uploadPartSize / fileChunkSize);
  const uint64_t uploadPartCount =
      std::max<uint64_t>(1, (totalChunks + chunksPerUploadPart - 1) /
                                 chunksPerUploadPart);

  const StartUploadResponse started = api_.startUpload(StartUploadRequest{
      .originalSize = static_cast<int64_t>(originalSize),
      .fileChunkSize = static_cast<int64_t>(fileChunkSize),
      .totalChunks = static_cast<int>(totalChunks),
      .uploadPartSize = static_cast<int64_t>(uploadPartSize),
      .uploadPartCount = static_cast<int>(uploadPartCount),
      .encryptionVersion = 1,
      .folderId = entry.parentFolderId,
  });

  std::vector<uint8_t> fileKey = fileEncryptor_.createFileKey();
  std::vector<uint8_t> encryptedFileKey;
  std::vector<uint8_t> encryptedMetadata;
  std::vector<uint8_t> encryptedManifest;
  std::vector<uint8_t> combinedChunkHashes;
  nlohmann::json manifestChunks = nlohmann::json::array();

  try {
    ArkiveFileReader reader(path, fileEncryptor_, fileKey, started.vaultId,
                            started.fileId,
                            static_cast<uint64_t>(started.fileChunkSize),
                            static_cast<uint64_t>(started.totalChunks));

    for (uint64_t partNumber = 1; partNumber <= uploadPartCount; ++partNumber) {
      std::vector<uint8_t> uploadBody;

      // File encryption stays chunk-based for AAD and manifests, but the
      // storage upload contract is part-based. Combine multiple encrypted
      // chunks into each upload part so server part numbers match the declared
      // uploadPartCount/uploadPartSize window.
      for (uint64_t chunkIndex = 0;
           chunkIndex < chunksPerUploadPart && reader.hasNextChunk();
           ++chunkIndex) {
        const EncryptedFileChunk chunk = reader.nextEncryptedChunk();
        const std::vector<uint8_t> encryptedHashBytes =
            fileEncryptor_.hashBytes(chunk.ciphertext);
        const std::string encryptedHash = encodeBase64(encryptedHashBytes);

        appendBytes(combinedChunkHashes, encryptedHashBytes);
        appendBytes(uploadBody, chunk.ciphertext);

        manifestChunks.push_back({
            {"n", chunk.chunkNo},
            {"plain_size", chunk.plaintextSize},
            {"cipher_size", chunk.ciphertext.size()},
            {"hash", encryptedHash},
        });
      }

      if (uploadBody.empty()) {
        throw std::runtime_error("upload part had no encrypted chunk payload");
      }

      const std::vector<uint8_t> uploadHashBytes =
          fileEncryptor_.hashBytes(uploadBody);
      const std::string uploadHash = encodeBase64(uploadHashBytes);

      const PresignPartsResponse presigned =
          api_.presignParts(started.uploadSessionId,
                            {static_cast<int>(partNumber)});
      const auto urlIt = presigned.urls.find(static_cast<int>(partNumber));
      if (urlIt == presigned.urls.end()) {
        throw std::runtime_error("missing presigned URL for upload part " +
                                 std::to_string(partNumber));
      }

      const std::string etag =
          api_.putEncryptedPartToStorage(urlIt->second,
                                         toByteVector(uploadBody));
      api_.uploadPart(started.uploadSessionId,
                      UploadPartRequest{
                          .partNumber = static_cast<int>(partNumber),
                          .encryptedHash = uploadHash,
                          .etag = etag,
                      });

      fileEncryptor_.zeroize(uploadBody);
    }

    encryptedMetadata = fileEncryptor_.encryptMetadata(
        buildMetadata(path, entry).dump(), fileKey, started.vaultId,
        started.fileId);

    const nlohmann::json manifest = {
        {"schema", "arkive.file.manifest"},
        {"version", 1},
        {"hash_alg", "blake3"},
        {"hash_encoding", "base64"},
        {"file_id", started.fileId},
        {"name", path.filename().string()},
        {"mime", detectMimeType(path)},
        {"extension", path.has_extension() ? path.extension().string() : ""},
        {"size", originalSize},
        {"chunk_size", static_cast<uint64_t>(started.fileChunkSize)},
        {"chunks", manifestChunks},
    };
    const std::string manifestJson = manifest.dump();
    encryptedManifest = fileEncryptor_.encryptChunk(
        std::vector<uint8_t>(manifestJson.begin(), manifestJson.end()),
        fileKey,
        ArkiveAad::toBytes(
            ArkiveAad::makeFileManifest(started.vaultId, started.fileId)));

    const std::vector<uint8_t> encryptedHash =
        fileEncryptor_.hashBytes(combinedChunkHashes);
    encryptedFileKey =
        fileEncryptor_.wrapFileKey(fileKey, started.vaultId, started.fileId);

    api_.uploadComplete(started.uploadSessionId,
                        UploadCompleteRequest{
                            .encryptedMetadata = encodeBase64(encryptedMetadata),
                            .encryptedFileKey = encodeBase64(encryptedFileKey),
                            .encryptedManifest = encodeBase64(encryptedManifest),
                            .encryptedHash = encodeBase64(encryptedHash),
                            .searchTokens = fileEncryptor_.createSearchTokenEntries(
                                started.vaultId, path.filename().string(),
                                detectMimeType(path)),
                            .hasThumbnail = false,
                            .thumbnailMime = "",
                            .thumbnailWidth = 0,
                            .thumbnailHeight = 0,
                        });
  } catch (...) {
    if (!fileKey.empty()) {
      fileEncryptor_.zeroize(fileKey);
    }
    if (!encryptedFileKey.empty()) {
      fileEncryptor_.zeroize(encryptedFileKey);
    }
    if (!encryptedMetadata.empty()) {
      fileEncryptor_.zeroize(encryptedMetadata);
    }
    if (!encryptedManifest.empty()) {
      fileEncryptor_.zeroize(encryptedManifest);
    }
    if (!combinedChunkHashes.empty()) {
      fileEncryptor_.zeroize(combinedChunkHashes);
    }
    throw;
  }

  if (!fileKey.empty()) {
    fileEncryptor_.zeroize(fileKey);
  }
  if (!encryptedFileKey.empty()) {
    fileEncryptor_.zeroize(encryptedFileKey);
  }
  if (!encryptedMetadata.empty()) {
    fileEncryptor_.zeroize(encryptedMetadata);
  }
  if (!encryptedManifest.empty()) {
    fileEncryptor_.zeroize(encryptedManifest);
  }
  if (!combinedChunkHashes.empty()) {
    fileEncryptor_.zeroize(combinedChunkHashes);
  }

  return UploadFileResponse{
      .fileId = started.fileId,
      .vaultId = started.vaultId,
      .uploadSessionId = started.uploadSessionId,
      .providerUploadId = started.providerUploadId,
  };
}
