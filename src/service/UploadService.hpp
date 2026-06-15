#pragma once

#include "api/ArkiveApi.hpp"
#include "fs/FileEncryptor.hpp"
#include "repo/EntryRepo.hpp"
#include "repo/UploadResumeRepo.hpp"
#include "upload/MultipartUploader.hpp"
#include "upload/UploadFinalizer.hpp"
#include <filesystem>
#include <string>

struct UploadFileResponse {
  std::string fileId;
  std::string vaultId;
  std::string uploadSessionId;
  std::string providerUploadId;
};

class IUploadService {
public:
  virtual ~IUploadService() = default;
  virtual UploadFileResponse uploadFile(const std::filesystem::path &path,
                                        const Entry &entry) = 0;
};

class UploadService : public IUploadService {
public:
  UploadService(ArkiveApi &api, FileEncryptor &fileEncryptor,
                UploadResumeRepo &uploadResumeRepo);

  UploadFileResponse uploadFile(const std::filesystem::path &path,
                                const Entry &entry) override;

private:
  ArkiveApi &api_;
  FileEncryptor &fileEncryptor_;
  UploadResumeRepo &uploadResumeRepo_;
  MultipartUploader multipartUploader_;
  UploadFinalizer uploadFinalizer_;
};
