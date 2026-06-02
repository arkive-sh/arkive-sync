#include "fs/ArkiveFileReader.hpp"

#include "crypto/Aad.hpp"
#include <stdexcept>

ArkiveFileReader::ArkiveFileReader(const std::filesystem::path &path,
                                   FileEncryptor &encryptor,
                                   const std::vector<uint8_t> &fileKey,
                                   const std::string &vaultId,
                                   const std::string &fileId,
                                   uint64_t chunkSize, uint64_t totalChunks)
    : filePath_(path), stream_(path, std::ios::binary), encryptor_(encryptor),
      fileKey_(fileKey), vaultId_(vaultId), fileId_(fileId),
      chunkSize_(chunkSize), totalChunks_(totalChunks) {
  if (!stream_.is_open()) {
    throw std::runtime_error("File cannot be opened");
  }
  if (fileKey_.empty()) {
    throw std::invalid_argument("fileKey cannot be empty");
  }
  if (chunkSize_ == 0) {
    throw std::invalid_argument("chunkSize cannot be zero");
  }
  if (totalChunks_ == 0) {
    throw std::invalid_argument("totalChunks cannot be zero");
  }
}

bool ArkiveFileReader::hasNextChunk() const noexcept {
  return nextChunkIndex_ < totalChunks_;
}

EncryptedFileChunk ArkiveFileReader::nextEncryptedChunk() {
  if (!hasNextChunk()) {
    throw std::runtime_error("No more chunks available");
  }

  std::vector<char> rawBuffer(static_cast<std::size_t>(chunkSize_));
  stream_.read(rawBuffer.data(), static_cast<std::streamsize>(rawBuffer.size()));
  const std::streamsize bytesRead = stream_.gcount();

  if (bytesRead <= 0) {
    throw std::runtime_error("Unable to read next file chunk");
  }

  std::vector<uint8_t> plaintextChunk(
      rawBuffer.begin(), rawBuffer.begin() + bytesRead);
  const uint64_t chunkNo = nextChunkIndex_ + 1;
  const std::vector<uint8_t> aad = ArkiveAad::toBytes(
      ArkiveAad::makeFileChunk(vaultId_, fileId_, static_cast<int>(chunkNo),
                               static_cast<int64_t>(chunkSize_),
                               static_cast<int>(totalChunks_)));
  const std::vector<uint8_t> ciphertext = encryptor_.encryptChunk(
      plaintextChunk, fileKey_, aad);

  ++nextChunkIndex_;

  return EncryptedFileChunk{
      .chunkNo = chunkNo,
      .plaintextSize = static_cast<uint64_t>(bytesRead),
      .ciphertext = ciphertext,
      .isLastChunk = nextChunkIndex_ == totalChunks_,
  };
}
