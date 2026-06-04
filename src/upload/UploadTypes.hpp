#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct UploadPlan {
  uint64_t originalSize = 0;
  uint64_t fileChunkSize = 0;
  uint64_t totalChunks = 0;
  uint64_t uploadPartSize = 0;
  uint64_t chunksPerUploadPart = 0;
  uint64_t uploadPartCount = 0;
};

struct UploadPartPlan {
  uint64_t partNumber = 0;
  uint64_t partStart = 0;
  uint64_t partEnd = 0;
  uint64_t firstChunkNumber = 0;
};

struct EncryptedChunkResult {
  uint64_t chunkNumber = 0;
  uint64_t plaintextSize = 0;
  uint64_t ciphertextSize = 0;
  std::string encryptedHash;
};

struct UploadedPartResult {
  UploadPartPlan plan;
  std::vector<EncryptedChunkResult> chunks;
  std::string uploadHash;
  std::string etag;
  std::vector<uint8_t> combinedChunkHashes;
};

struct UploadArtifacts {
  std::vector<uint8_t> fileKey;
  std::vector<uint8_t> encryptedFileKey;
  std::vector<uint8_t> encryptedMetadata;
  std::vector<uint8_t> encryptedManifest;
  std::vector<uint8_t> encryptedHash;
};
