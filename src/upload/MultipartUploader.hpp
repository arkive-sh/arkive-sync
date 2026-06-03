#pragma once

#include "api/ArkiveApi.hpp"
#include "upload/UploadTypes.hpp"
#include <filesystem>
#include <vector>

class FileEncryptor;

class MultipartUploader {
public:
  MultipartUploader(ArkiveApi &api, FileEncryptor &fileEncryptor);

  std::vector<UploadedPartResult>
  uploadParts(const std::filesystem::path &path,
              const std::vector<uint8_t> &fileKey,
              const StartUploadResponse &started,
              const UploadPlan &plan,
              uint64_t partConcurrency);

private:
  ArkiveApi &api_;
  FileEncryptor &fileEncryptor_;
};
