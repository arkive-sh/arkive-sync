#include "fs/DecryptedFileWriter.hpp"

#include <stdexcept>

DecryptedFileWriter::DecryptedFileWriter(const std::filesystem::path &path,
                                         RustCrypto &crypto)
    : filePath_(path), stream_(path, std::ios::binary | std::ios::trunc),
      crypto_(crypto) {
  if (!stream_.is_open()) {
    throw std::runtime_error("File cannot be opened");
  }
}

void DecryptedFileWriter::decryptAndWriteChunk(
    const std::vector<uint8_t> &encryptedChunk,
    const std::vector<uint8_t> &fileKey, const std::vector<uint8_t> &aad) {
  if (fileKey.empty()) {
    throw std::invalid_argument("fileKey cannot be empty");
  }

  const std::vector<uint8_t> plaintext =
      crypto_.decryptChunk(fileKey, aad, encryptedChunk);

  stream_.write(reinterpret_cast<const char *>(plaintext.data()),
                static_cast<std::streamsize>(plaintext.size()));
  if (!stream_) {
    throw std::runtime_error("Failed to write plaintext chunk");
  }
}
