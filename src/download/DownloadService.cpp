#include "download/DownloadService.hpp"

#include "crypto/Aad.hpp"
#include "platform/AtomicFileWriterFactory.hpp"

#include <cstdint>
#include <spdlog/spdlog.h>
#include <stdexcept>

DownloadService::DownloadService(ArkiveApi &api, ArkiveHttpClient &httpClient,
                                 RustCrypto &crypto,
                                 DownloadRecordDecryptor &recordDecryptor)
    : api_(api), httpClient_(httpClient), crypto_(crypto),
      recordDecryptor_(recordDecryptor) {}

void DownloadService::downloadFile(
    const std::string &fileId, const std::filesystem::path &targetPath) const {
  const FileRecordResponse record = api_.getFileRecord(fileId);
  const DecryptedDownloadRecord decrypted = recordDecryptor_.decrypt(record);

  auto writer = createAtomicFileWriter(targetPath);
  writer->preallocate(decrypted.manifest.size);
  spdlog::info("Starting download file={} path={} size={} chunks={}",
               record.fileId, targetPath.string(), decrypted.manifest.size,
               decrypted.manifest.chunks.size());

  try {
    std::uint64_t lastLoggedBytes = 0;
    constexpr std::uint64_t kProgressLogInterval = 256ULL * 1024ULL * 1024ULL;

    for (const auto &chunk : decrypted.manifest.chunks) {
      std::vector<uint8_t> encrypted;
      encrypted.reserve(chunk.cipherSize);
      httpClient_.getRangeToSink(
          record.sourceUrl, chunk.cipherStart, chunk.cipherSize,
          [&](const uint8_t *data, std::size_t size) {
            encrypted.insert(encrypted.end(), data, data + size);
          });

      const auto plaintext = crypto_.decryptChunk(
          decrypted.fileKey,
          ArkiveAad::toBytes(ArkiveAad::makeFileChunk(
              record.vaultId, record.fileId, static_cast<int>(chunk.index + 1),
              record.chunkSize, record.totalChunks)),
          encrypted);
      writer->writeAt(chunk.plainStart, plaintext);
      const std::uint64_t downloadedBytes = chunk.plainStart + plaintext.size();
      if (downloadedBytes == decrypted.manifest.size ||
          downloadedBytes - lastLoggedBytes >= kProgressLogInterval) {
        spdlog::info("Downloading file={} path={} chunk={}/{} bytes={}/{}",
                     record.fileId, targetPath.string(), chunk.index + 1,
                     decrypted.manifest.chunks.size(), downloadedBytes,
                     decrypted.manifest.size);
        lastLoggedBytes = downloadedBytes;
      }
    }
    writer->commit();
  } catch (...) {
    writer->rollback();
    throw;
  }
}
