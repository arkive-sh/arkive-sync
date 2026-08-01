#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct DownloadChunk {
  uint64_t index = 0;
  uint64_t cipherStart = 0;
  uint64_t cipherEnd = 0;
  uint64_t cipherSize = 0;
  uint64_t plainStart = 0;
  uint64_t plainSize = 0;
  std::string hash;
};

struct DownloadManifest {
  uint64_t size = 0;
  uint64_t chunkSize = 0;
  std::string name;
  std::string mime;
  std::vector<DownloadChunk> chunks;
};

DownloadManifest parseDownloadManifest(const std::string &manifestJson,
                                       uint64_t fallbackChunkSize,
                                       uint64_t plaintextSize);
