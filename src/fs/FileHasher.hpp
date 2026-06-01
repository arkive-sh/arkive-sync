#pragma once

#include "crypto/RustCrypto.hpp"
#include <filesystem>
#include <string>

inline constexpr std::size_t kChunkSize = 4 * 1024 * 1024; // 4MB

class FileHasher {
public:
  explicit FileHasher(const std::filesystem::path &path);

  std::string hashFile();

private:
  const std::filesystem::path path_;
  RustCrypto crypto_;
};
