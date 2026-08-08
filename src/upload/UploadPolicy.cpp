#include "upload/UploadPolicy.hpp"

#include <algorithm>

namespace ArkiveUploadPolicy {

uint64_t resolveFileConcurrency(uint64_t fileSize) {
  constexpr uint64_t kMiB = 1024 * 1024;
  if (fileSize <= 4 * kMiB) {
    return 32;
  }
  if (fileSize <= 64 * kMiB) {
    return 8;
  }
  if (fileSize <= 512 * kMiB) {
    return 4;
  }
  return 2;
}

uint64_t resolvePartConcurrency(uint64_t uploadPartCount, int serverCap) {
  if (uploadPartCount == 0) {
    return 0;
  }

  const uint64_t effectiveServerCap =
      serverCap > 0 ? static_cast<uint64_t>(serverCap)
                    : kDefaultDesktopPartConcurrency;

  return std::max<uint64_t>(
      1, std::min({uploadPartCount, kDefaultDesktopPartConcurrency,
                   effectiveServerCap}));
}

} // namespace ArkiveUploadPolicy
