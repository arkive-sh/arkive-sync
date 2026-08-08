#pragma once

#include "api/ArkiveApi.hpp"
#include "upload/UploadTypes.hpp"
#include <filesystem>
#include <functional>
#include <vector>

class FileEncryptor;

struct UploadTiming {
  uint64_t prepareMs = 0;
  uint64_t presignMs = 0;
  uint64_t storageMs = 0;
  uint64_t resumeMs = 0;
};

class MultipartUploader {
public:
  MultipartUploader(ArkiveApi &api, FileEncryptor &fileEncryptor);

  std::vector<UploadedPartResult>
  uploadParts(const std::filesystem::path &path,
              const std::vector<uint8_t> &fileKey,
              const StartUploadResponse &started,
              const UploadPlan &plan,
              uint64_t partConcurrency,
              const std::vector<UploadedPartResult> &completedParts = {},
              const std::function<void(const UploadedPartResult &)> &onPartUploaded = {},
              UploadTiming *timing = nullptr);

private:
  ArkiveApi &api_;
  FileEncryptor &fileEncryptor_;
};
