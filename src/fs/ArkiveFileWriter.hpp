#pragma once

#include "crypto/Aad.hpp"
#include "crypto/RustCrypto.hpp"
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

class ArkiveFileWriter {
public:
  ArkiveFileWriter(const std::filesystem::path &path, RustCrypto &crypto);

  ArkiveFileWriter(const ArkiveFileWriter &) = delete;
  ArkiveFileWriter &operator=(const ArkiveFileWriter &) = delete;
  ArkiveFileWriter(ArkiveFileWriter &&) = delete;
  ArkiveFileWriter &operator=(ArkiveFileWriter &&) = delete;

  void writeEncryptedChunk(const std::vector<uint8_t> &encryptedChunk,
                           const std::vector<uint8_t> &fileKey,
                           const std::vector<uint8_t> &aad);

private:
  std::filesystem::path filePath_;
  std::ofstream stream_;
  RustCrypto &crypto_;
};
