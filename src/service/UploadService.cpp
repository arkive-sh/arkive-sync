#include "service/UploadService.hpp"

#include "crypto/Aad.hpp"
#include "fs/FileHasher.hpp"
#include "helpers/Mime.hpp"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <thread>
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

struct UploadedChunkManifestEntry {
  uint64_t chunkNo;
  uint64_t plaintextSize;
  uint64_t ciphertextSize;
  std::string encryptedHash;
};

struct UploadedPartResult {
  std::vector<UploadedChunkManifestEntry> chunks;
  std::vector<uint8_t> combinedChunkHashes;
};

nlohmann::json buildMetadata(const std::filesystem::path &path,
                             const EntryRecord &entry) {
  const std::string mime = inferSafeMimeType(path);
  return {
      {"schema", "arkive.file.metadata"},
      {"version", 1},
      {"name", path.filename().string()},
      {"mime", mime},
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
  const uint64_t uploadPartCount = std::max<uint64_t>(
      1, (totalChunks + chunksPerUploadPart - 1) / chunksPerUploadPart);
  const UploadLimitsResponse limits = api_.uploadLimits();
  const uint64_t partConcurrency = std::max<uint64_t>(
      1, std::min<uint64_t>(uploadPartCount, static_cast<uint64_t>(std::max(
                                                 1, limits.partConcurrency))));

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
  std::vector<UploadedPartResult> uploadedParts(
      static_cast<std::size_t>(uploadPartCount));

  try {
    std::atomic<uint64_t> nextPartNumber{1};
    std::mutex resultsMutex;
    std::mutex errorMutex;
    std::exception_ptr firstError;
    std::atomic<bool> stopWorkers{false};

    auto uploadWorker = [&]() {
      while (!stopWorkers.load()) {
        const uint64_t partNumber = nextPartNumber.fetch_add(1);
        if (partNumber > uploadPartCount) {
          return;
        }

        std::vector<uint8_t> uploadBody;
        UploadedPartResult partResult;

        try {
          const uint64_t partStart = (partNumber - 1) * uploadPartSize;
          const uint64_t partEnd =
              std::min<uint64_t>(partStart + uploadPartSize, originalSize);
          const uint64_t firstChunkNumber = (partStart / fileChunkSize) + 1;
          std::ifstream stream(path, std::ios::binary);
          if (!stream.is_open()) {
            throw std::runtime_error("File cannot be opened");
          }
          stream.seekg(static_cast<std::streamoff>(partStart), std::ios::beg);
          if (!stream.good()) {
            throw std::runtime_error("Unable to seek to requested upload part");
          }

          // Desktop can use real threads here, but it still respects the
          // server-declared partConcurrency window by only running that many
          // upload workers per file at once.
          for (uint64_t chunkStart = partStart, chunkNumber = firstChunkNumber;
               chunkStart < partEnd;
               chunkStart += fileChunkSize, ++chunkNumber) {

            const uint64_t chunkEnd =
                std::min<uint64_t>(chunkStart + fileChunkSize, partEnd);

            std::vector<uint8_t> plaintextChunk(
                static_cast<std::size_t>(chunkEnd - chunkStart));
            stream.read(reinterpret_cast<char *>(plaintextChunk.data()),
                        static_cast<std::streamsize>(plaintextChunk.size()));

            const std::streamsize bytesRead = stream.gcount();
            if (bytesRead <= 0 ||
                static_cast<uint64_t>(bytesRead) != chunkEnd - chunkStart) {
              throw std::runtime_error("Unable to read upload part chunk");
            }

            const std::vector<uint8_t> aad =
                ArkiveAad::toBytes(ArkiveAad::makeFileChunk(
                    started.vaultId, started.fileId,
                    static_cast<int>(chunkNumber),
                    static_cast<int64_t>(started.fileChunkSize),
                    static_cast<int>(started.totalChunks)));

            const std::vector<uint8_t> ciphertext =
                fileEncryptor_.encryptChunk(plaintextChunk, fileKey, aad);
            fileEncryptor_.zeroize(plaintextChunk);

            const std::vector<uint8_t> encryptedHashBytes =
                fileEncryptor_.hashBytes(ciphertext);
            const std::string encryptedHash = encodeBase64(encryptedHashBytes);

            appendBytes(partResult.combinedChunkHashes, encryptedHashBytes);
            appendBytes(uploadBody, ciphertext);
            partResult.chunks.push_back({
                .chunkNo = chunkNumber,
                .plaintextSize = chunkEnd - chunkStart,
                .ciphertextSize = ciphertext.size(),
                .encryptedHash = encryptedHash,
            });
          }

          if (uploadBody.empty()) {
            throw std::runtime_error(
                "upload part had no encrypted chunk payload");
          }

          const std::vector<uint8_t> uploadHashBytes =
              fileEncryptor_.hashBytes(uploadBody);
          const std::string uploadHash = encodeBase64(uploadHashBytes);

          const PresignPartsResponse presigned = api_.presignParts(
              started.uploadSessionId, {static_cast<int>(partNumber)});

          const auto urlIt = presigned.urls.find(static_cast<int>(partNumber));
          if (urlIt == presigned.urls.end()) {
            throw std::runtime_error("missing presigned URL for upload part " +
                                     std::to_string(partNumber));
          }

          const std::string etag = api_.putEncryptedPartToStorage(
              urlIt->second, toByteVector(uploadBody));
          api_.uploadPart(started.uploadSessionId,
                          UploadPartRequest{
                              .partNumber = static_cast<int>(partNumber),
                              .encryptedHash = uploadHash,
                              .etag = etag,
                          });

          {
            std::lock_guard<std::mutex> lock(resultsMutex);
            uploadedParts[static_cast<std::size_t>(partNumber - 1)] =
                std::move(partResult);
          }
        } catch (...) {
          stopWorkers.store(true);
          std::lock_guard<std::mutex> lock(errorMutex);
          if (!firstError) {
            firstError = std::current_exception();
          }
        }

        if (!uploadBody.empty()) {
          fileEncryptor_.zeroize(uploadBody);
        }
      }
    };

    std::vector<std::thread> workers;
    workers.reserve(static_cast<std::size_t>(partConcurrency));
    for (uint64_t workerIndex = 0; workerIndex < partConcurrency;
         ++workerIndex) {
      workers.emplace_back(uploadWorker);
    }
    for (auto &worker : workers) {
      worker.join();
    }
    if (firstError) {
      std::rethrow_exception(firstError);
    }

    nlohmann::json manifestChunks = nlohmann::json::array();
    for (const auto &part : uploadedParts) {
      for (const auto &chunk : part.chunks) {
        manifestChunks.push_back({
            {"n", chunk.chunkNo},
            {"plain_size", chunk.plaintextSize},
            {"cipher_size", chunk.ciphertextSize},
            {"hash", chunk.encryptedHash},
        });
      }
      appendBytes(combinedChunkHashes, part.combinedChunkHashes);
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
        {"mime", inferSafeMimeType(path)},
        {"extension", path.has_extension() ? path.extension().string() : ""},
        {"size", originalSize},
        {"chunk_size", static_cast<uint64_t>(started.fileChunkSize)},
        {"chunks", manifestChunks},
    };
    const std::string manifestJson = manifest.dump();
    encryptedManifest = fileEncryptor_.encryptChunk(
        std::vector<uint8_t>(manifestJson.begin(), manifestJson.end()), fileKey,
        ArkiveAad::toBytes(
            ArkiveAad::makeFileManifest(started.vaultId, started.fileId)));

    const std::vector<uint8_t> encryptedHash =
        fileEncryptor_.hashBytes(combinedChunkHashes);
    encryptedFileKey =
        fileEncryptor_.wrapFileKey(fileKey, started.vaultId, started.fileId);

    api_.uploadComplete(
        started.uploadSessionId,
        UploadCompleteRequest{
            .encryptedMetadata = encodeBase64(encryptedMetadata),
            .encryptedFileKey = encodeBase64(encryptedFileKey),
            .encryptedManifest = encodeBase64(encryptedManifest),
            .encryptedHash = encodeBase64(encryptedHash),
            .searchTokens = fileEncryptor_.createSearchTokenEntries(
                started.vaultId, path.filename().string(),
                inferSafeMimeType(path)),
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
