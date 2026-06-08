#pragma once

#include "crypto/Aad.hpp"
#include "crypto/RustCrypto.hpp"
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

class DecryptedFileWriter {
public:
  DecryptedFileWriter(const std::filesystem::path &path, RustCrypto &crypto);

  DecryptedFileWriter(const DecryptedFileWriter &) = delete;
  DecryptedFileWriter &operator=(const DecryptedFileWriter &) = delete;
  DecryptedFileWriter(DecryptedFileWriter &&) = delete;
  DecryptedFileWriter &operator=(DecryptedFileWriter &&) = delete;

  void decryptAndWriteChunk(const std::vector<uint8_t> &encryptedChunk,
                            const std::vector<uint8_t> &fileKey,
                            const std::vector<uint8_t> &aad);

private:
  std::filesystem::path filePath_;
  std::ofstream stream_;
  RustCrypto &crypto_;
};
