#pragma once

#include <cstdint>

namespace ArkiveUploadPolicy {

inline constexpr uint64_t kDefaultDesktopPartConcurrency = 6;

uint64_t resolvePartConcurrency(uint64_t uploadPartCount, int serverCap);

} // namespace ArkiveUploadPolicy
