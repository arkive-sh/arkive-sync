#pragma once

#include "api/ArkiveApi.hpp"
#include "fs/FileEncryptor.hpp"
#include "repo/EntryRepo.hpp"
#include "repo/UploadResumeRepo.hpp"
#include "upload/MultipartUploader.hpp"
#include "upload/UploadFinalizer.hpp"
#include <filesystem>
#include <exception>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct UploadFileResponse {
  std::string fileId;
  std::string vaultId;
  std::string uploadSessionId;
  std::string providerUploadId;
};

struct UploadBatchItem {
  std::string id;
  std::filesystem::path path;
  Entry entry;
};

struct UploadBatchResult {
  std::string id;
  std::optional<UploadFileResponse> response;
  std::string error;
};

class IUploadService {
public:
  virtual ~IUploadService() = default;
  virtual UploadFileResponse uploadFile(const std::filesystem::path &path,
                                        const Entry &entry) = 0;
  virtual std::vector<UploadBatchResult> uploadFilesBatch(
      const std::vector<UploadBatchItem> &items) {
    std::vector<UploadBatchResult> results;
    for (const auto &item : items) {
      try {
        results.push_back(UploadBatchResult{
            .id = item.id,
            .response = uploadFile(item.path, item.entry),
        });
      } catch (const std::exception &error) {
        results.push_back(UploadBatchResult{
            .id = item.id,
            .error = error.what(),
        });
      }
    }
    return results;
  }
};

class UploadService : public IUploadService {
public:
  UploadService(ArkiveApi &api, FileEncryptor &fileEncryptor,
                UploadResumeRepo &uploadResumeRepo);

  UploadFileResponse uploadFile(const std::filesystem::path &path,
                                const Entry &entry) override;
  std::vector<UploadBatchResult> uploadFilesBatch(
      const std::vector<UploadBatchItem> &items) override;

private:
  UploadLimitsResponse uploadLimits();

  ArkiveApi &api_;
  FileEncryptor &fileEncryptor_;
  UploadResumeRepo &uploadResumeRepo_;
  MultipartUploader multipartUploader_;
  UploadFinalizer uploadFinalizer_;
  std::once_flag uploadLimitsOnce_;
  UploadLimitsResponse cachedUploadLimits_{};
};
