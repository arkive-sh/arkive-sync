#pragma once

#include "api/ArkiveApi.hpp"
#include "repo/EntryRepo.hpp"
#include "upload/ThumbnailGenerator.hpp"
#include "upload/UploadTypes.hpp"
#include <filesystem>
#include <functional>
#include <optional>
#include <vector>

class FileEncryptor;

struct PreparedUploadCompletion {
  UploadArtifacts artifacts;
  UploadCompleteRequest request;
};

class UploadFinalizer {
public:
  using ThumbnailGenerator =
      std::function<std::optional<UploadThumbnail>(const std::filesystem::path &)>;

  UploadFinalizer(ArkiveApi &api, FileEncryptor &fileEncryptor);
  UploadFinalizer(ArkiveApi &api, FileEncryptor &fileEncryptor,
                  ThumbnailGenerator thumbnailGenerator);

  PreparedUploadCompletion prepareCompletion(
      const std::filesystem::path &path, const Entry &entry,
      const StartUploadResponse &started, const UploadPlan &plan,
      const std::vector<UploadedPartResult> &parts,
      const std::vector<uint8_t> &fileKey);

  UploadArtifacts completeUpload(const std::filesystem::path &path,
                                 const Entry &entry,
                                 const StartUploadResponse &started,
                                 const UploadPlan &plan,
                                 const std::vector<UploadedPartResult> &parts,
                                 const std::vector<uint8_t> &fileKey);

private:
  ArkiveApi &api_;
  FileEncryptor &fileEncryptor_;
  ThumbnailGenerator thumbnailGenerator_;
};
