#pragma once

#include "api/ArkiveApi.hpp"
#include "repo/SyncRepo.hpp"
#include "upload/UploadTypes.hpp"
#include <filesystem>
#include <vector>

class FileEncryptor;

class UploadFinalizer {
public:
  UploadFinalizer(ArkiveApi &api, FileEncryptor &fileEncryptor);

  UploadArtifacts completeUpload(const std::filesystem::path &path,
                                 const EntryRecord &entry,
                                 const StartUploadResponse &started,
                                 const UploadPlan &plan,
                                 const std::vector<UploadedPartResult> &parts,
                                 const std::vector<uint8_t> &fileKey);

private:
  ArkiveApi &api_;
  FileEncryptor &fileEncryptor_;
};
