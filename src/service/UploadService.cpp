#include "service/UploadService.hpp"

#include "upload/UploadPolicy.hpp"
#include "upload/UploadPlanner.hpp"
#include <stdexcept>

UploadService::UploadService(ArkiveApi &api, FileEncryptor &fileEncryptor)
    : api_(api), fileEncryptor_(fileEncryptor),
      multipartUploader_(api, fileEncryptor),
      uploadFinalizer_(api, fileEncryptor) {}

UploadFileResponse UploadService::uploadFile(const std::filesystem::path &path,
                                             const EntryRecord &entry) {
  if (entry.isDirectory) {
    throw std::invalid_argument("uploadFile does not support directories");
  }
  if (!entry.localSize.has_value() || *entry.localSize < 0) {
    throw std::invalid_argument("uploadFile requires a valid local file size");
  }

  const uint64_t originalSize = static_cast<uint64_t>(*entry.localSize);
  const UploadPlan uploadPlan = UploadPlanner::createPlan(originalSize);
  const UploadLimitsResponse limits = api_.uploadLimits();
  const uint64_t partConcurrency = ArkiveUploadPolicy::resolvePartConcurrency(
      uploadPlan.uploadPartCount, limits.partConcurrency);

  const StartUploadResponse started = api_.startUpload(StartUploadRequest{
      .originalSize = static_cast<int64_t>(uploadPlan.originalSize),
      .fileChunkSize = static_cast<int64_t>(uploadPlan.fileChunkSize),
      .totalChunks = static_cast<int>(uploadPlan.totalChunks),
      .uploadPartSize = static_cast<int64_t>(uploadPlan.uploadPartSize),
      .uploadPartCount = static_cast<int>(uploadPlan.uploadPartCount),
      .encryptionVersion = 1,
      .folderId = entry.parentFolderId,
  });

  UploadArtifacts artifacts;

  try {
    artifacts.fileKey = fileEncryptor_.createFileKey();
    const std::vector<UploadedPartResult> uploadedParts =
        multipartUploader_.uploadParts(path, artifacts.fileKey, started,
                                       uploadPlan, partConcurrency);
    artifacts = uploadFinalizer_.completeUpload(path, entry, started, uploadPlan,
                                                uploadedParts, artifacts.fileKey);
  } catch (...) {
    if (!artifacts.fileKey.empty()) {
      fileEncryptor_.zeroize(artifacts.fileKey);
    }
    if (!artifacts.encryptedFileKey.empty()) {
      fileEncryptor_.zeroize(artifacts.encryptedFileKey);
    }
    if (!artifacts.encryptedMetadata.empty()) {
      fileEncryptor_.zeroize(artifacts.encryptedMetadata);
    }
    if (!artifacts.encryptedManifest.empty()) {
      fileEncryptor_.zeroize(artifacts.encryptedManifest);
    }
    if (!artifacts.encryptedHash.empty()) {
      fileEncryptor_.zeroize(artifacts.encryptedHash);
    }
    throw;
  }

  if (!artifacts.fileKey.empty()) {
    fileEncryptor_.zeroize(artifacts.fileKey);
  }
  if (!artifacts.encryptedFileKey.empty()) {
    fileEncryptor_.zeroize(artifacts.encryptedFileKey);
  }
  if (!artifacts.encryptedMetadata.empty()) {
    fileEncryptor_.zeroize(artifacts.encryptedMetadata);
  }
  if (!artifacts.encryptedManifest.empty()) {
    fileEncryptor_.zeroize(artifacts.encryptedManifest);
  }
  if (!artifacts.encryptedHash.empty()) {
    fileEncryptor_.zeroize(artifacts.encryptedHash);
  }

  return UploadFileResponse{
      .fileId = started.fileId,
      .vaultId = started.vaultId,
      .uploadSessionId = started.uploadSessionId,
      .providerUploadId = started.providerUploadId,
  };
}
