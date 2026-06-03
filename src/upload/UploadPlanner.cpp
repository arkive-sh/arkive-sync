#include "upload/UploadPlanner.hpp"

#include "fs/FileHasher.hpp"
#include <algorithm>

namespace {

constexpr uint64_t kMultipartUploadPartSize = 8ull * 1024ull * 1024ull;

} // namespace

UploadPlan UploadPlanner::createPlan(uint64_t plaintextSize) {
  const uint64_t fileChunkSize = static_cast<uint64_t>(kChunkSize);
  const uint64_t totalChunks = std::max<uint64_t>(
      1, (plaintextSize + fileChunkSize - 1) / fileChunkSize);
  const uint64_t uploadPartSize =
      totalChunks > 1 ? kMultipartUploadPartSize : fileChunkSize;
  const uint64_t chunksPerUploadPart =
      std::max<uint64_t>(1, uploadPartSize / fileChunkSize);
  const uint64_t uploadPartCount = std::max<uint64_t>(
      1, (totalChunks + chunksPerUploadPart - 1) / chunksPerUploadPart);

  return UploadPlan{
      .originalSize = plaintextSize,
      .fileChunkSize = fileChunkSize,
      .totalChunks = totalChunks,
      .uploadPartSize = uploadPartSize,
      .chunksPerUploadPart = chunksPerUploadPart,
      .uploadPartCount = uploadPartCount,
  };
}
