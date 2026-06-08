#include "fs/UploadFileChunkReader.hpp"

#include "crypto/Aad.hpp"
#include <stdexcept>

UploadFileChunkReader::UploadFileChunkReader(
    const std::filesystem::path &path, FileEncryptor &encryptor,
    const std::vector<uint8_t> &fileKey, const std::string &vaultId,
    const std::string &fileId, uint64_t chunkSize, uint64_t totalChunks)
    : filePath_(path), stream_(path, std::ios::binary), encryptor_(encryptor),
      fileKey_(fileKey), vaultId_(vaultId), fileId_(fileId),
      chunkSize_(chunkSize), totalChunks_(totalChunks),
      endChunkIndex_(totalChunks) {
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

UploadFileChunkReader::UploadFileChunkReader(
    const std::filesystem::path &path, FileEncryptor &encryptor,
    const std::vector<uint8_t> &fileKey, const std::string &vaultId,
    const std::string &fileId, uint64_t chunkSize, uint64_t totalChunks,
    uint64_t firstChunkNumber, uint64_t chunkCount)
    : filePath_(path), stream_(path, std::ios::binary), encryptor_(encryptor),
      fileKey_(fileKey), vaultId_(vaultId), fileId_(fileId),
      chunkSize_(chunkSize), totalChunks_(totalChunks),
      nextChunkIndex_(firstChunkNumber > 0 ? firstChunkNumber - 1 : 0),
      endChunkIndex_((firstChunkNumber > 0 ? firstChunkNumber - 1 : 0) +
                     chunkCount) {
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
  if (firstChunkNumber == 0) {
    throw std::invalid_argument("firstChunkNumber cannot be zero");
  }
  if (chunkCount == 0) {
    throw std::invalid_argument("chunkCount cannot be zero");
  }
  if (nextChunkIndex_ >= totalChunks_) {
    throw std::invalid_argument("firstChunkNumber exceeds totalChunks");
  }
  if (endChunkIndex_ > totalChunks_) {
    throw std::invalid_argument("chunk window exceeds totalChunks");
  }
  const uint64_t startOffset = nextChunkIndex_ * chunkSize_;
  stream_.seekg(static_cast<std::streamoff>(startOffset), std::ios::beg);
  if (!stream_.good()) {
    throw std::runtime_error("Unable to seek to requested chunk window");
  }
}

bool UploadFileChunkReader::hasNextChunk() const noexcept {
  return nextChunkIndex_ < endChunkIndex_;
}

UploadEncryptedChunk UploadFileChunkReader::nextEncryptedChunk() {
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

  return UploadEncryptedChunk{
      .chunkNo = chunkNo,
      .plaintextSize = static_cast<uint64_t>(bytesRead),
      .ciphertext = ciphertext,
      .isLastChunk = nextChunkIndex_ == totalChunks_,
  };
}
