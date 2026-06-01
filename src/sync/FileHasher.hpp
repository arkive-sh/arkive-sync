#pragma once
#include "../crypto/RustCrypto.hpp"
#include <filesystem>
#include <string>

class FileHasher {
public:
  explicit FileHasher(const std::filesystem::path &path);

  std::string hashFile();

private:
  const std::filesystem::path path_;
  RustCrypto crypto_;
};
