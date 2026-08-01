#include "download/DownloadManifest.hpp"

#include <nlohmann/json.hpp>
#include <stdexcept>

DownloadManifest parseDownloadManifest(const std::string &manifestJson,
                                       uint64_t fallbackChunkSize,
                                       uint64_t plaintextSize) {
  const auto json = nlohmann::json::parse(manifestJson);
  DownloadManifest manifest{
      .size = json.value("size", plaintextSize),
      .chunkSize = json.value("chunk_size", fallbackChunkSize),
      .name = json.value("name", ""),
      .mime = json.value("mime", ""),
  };

  if (!json.contains("chunks") || !json["chunks"].is_array()) {
    throw std::invalid_argument("Download manifest is missing chunks");
  }

  uint64_t cipherOffset = 0;
  uint64_t plainOffset = 0;
  uint64_t index = 0;
  for (const auto &chunkJson : json["chunks"]) {
    const uint64_t cipherSize = chunkJson.value("cipher_size", uint64_t{0});
    uint64_t plainSize = chunkJson.value("plain_size", uint64_t{0});
    if (plainSize == 0 && fallbackChunkSize > 0) {
      plainSize = fallbackChunkSize;
      if (plaintextSize > plainOffset &&
          plaintextSize - plainOffset < plainSize) {
        plainSize = plaintextSize - plainOffset;
      }
    }
    if (cipherSize == 0 || plainSize == 0) {
      throw std::invalid_argument("Download manifest has an empty chunk");
    }

    manifest.chunks.push_back(DownloadChunk{
        .index = index,
        .cipherStart = cipherOffset,
        .cipherEnd = cipherOffset + cipherSize - 1,
        .cipherSize = cipherSize,
        .plainStart = plainOffset,
        .plainSize = plainSize,
        .hash = chunkJson.value("hash", ""),
    });
    cipherOffset += cipherSize;
    plainOffset += plainSize;
    ++index;
  }

  return manifest;
}
