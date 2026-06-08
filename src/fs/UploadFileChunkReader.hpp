#pragma once

#include "fs/FileEncryptor.hpp"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

struct UploadEncryptedChunk {
  uint64_t chunkNo;
  uint64_t plaintextSize;
  std::vector<uint8_t> ciphertext;
  bool isLastChunk;
};

class UploadFileChunkReader {
public:
  UploadFileChunkReader(const std::filesystem::path &path,
                        FileEncryptor &encryptor,
                        const std::vector<uint8_t> &fileKey,
                        const std::string &vaultId,
                        const std::string &fileId, uint64_t chunkSize,
                        uint64_t totalChunks);
  UploadFileChunkReader(const std::filesystem::path &path,
                        FileEncryptor &encryptor,
                        const std::vector<uint8_t> &fileKey,
                        const std::string &vaultId,
                        const std::string &fileId, uint64_t chunkSize,
                        uint64_t totalChunks, uint64_t firstChunkNumber,
                        uint64_t chunkCount);

  UploadFileChunkReader(const UploadFileChunkReader &) = delete;
  UploadFileChunkReader &operator=(const UploadFileChunkReader &) = delete;
  UploadFileChunkReader(UploadFileChunkReader &&) = delete;
  UploadFileChunkReader &operator=(UploadFileChunkReader &&) = delete;

  bool hasNextChunk() const noexcept;
  UploadEncryptedChunk nextEncryptedChunk();

private:
  std::filesystem::path filePath_;
  std::ifstream stream_;
  FileEncryptor &encryptor_;
  std::vector<uint8_t> fileKey_;
  std::string vaultId_;
  std::string fileId_;
  uint64_t chunkSize_;
  uint64_t totalChunks_;
  uint64_t nextChunkIndex_ = 0;
  uint64_t endChunkIndex_ = 0;
};
