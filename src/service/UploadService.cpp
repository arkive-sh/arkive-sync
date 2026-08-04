#include "service/UploadService.hpp"

#include "api/HttpError.hpp"
#include "fs/helpers/PathHelpers.hpp"
#include "helpers/Base64.hpp"
#include "upload/UploadPlanner.hpp"
#include "upload/UploadPolicy.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace {

std::string toMtimeString(const std::filesystem::file_time_type &time) {
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      time.time_since_epoch())
                      .count();
  return std::to_string(static_cast<long long>(ms));
}

std::optional<std::string>
currentMtimeString(const std::filesystem::path &path) {
  std::error_code error;
  const auto mtime = std::filesystem::last_write_time(path, error);
  if (error) {
    return std::nullopt;
  }
  return toMtimeString(mtime);
}

UploadPartPlan makePartPlan(uint64_t partNumber, const UploadPlan &plan) {
  const uint64_t partStart = (partNumber - 1) * plan.uploadPartSize;
  const uint64_t partEnd =
      std::min<uint64_t>(partStart + plan.uploadPartSize, plan.originalSize);
  return UploadPartPlan{
      .partNumber = partNumber,
      .partStart = partStart,
      .partEnd = partEnd,
      .firstChunkNumber = (partStart / plan.fileChunkSize) + 1,
  };
}

bool shouldResetResumeSession(const HttpError &error) {
  return error.statusCode == 404 || error.statusCode == 409;
}

bool sessionMatchesEntry(const UploadResumeSessionRecord &session,
                         const Entry &entry, const std::string &localPath,
                         uint64_t localSize,
                         const std::optional<std::string> &localMtime,
                         const UploadPlan &plan) {
  if (session.localPath != localPath ||
      session.localSize != static_cast<int64_t>(localSize)) {
    return false;
  }
  if (session.folderId != entry.parentFolderId) {
    return false;
  }
  if (session.fileChunkSize != static_cast<int64_t>(plan.fileChunkSize) ||
      session.totalChunks != static_cast<int>(plan.totalChunks) ||
      session.uploadPartSize != static_cast<int64_t>(plan.uploadPartSize) ||
      session.uploadPartCount != static_cast<int>(plan.uploadPartCount)) {
    return false;
  }
  if (session.localHash.has_value() && entry.contentHash.has_value()) {
    return session.localHash == entry.contentHash;
  }
  return session.localMtime == localMtime;
}

UploadResumePartRecord makeResumePartRecord(const UploadedPartResult &part) {
  nlohmann::json chunks = nlohmann::json::array();
  for (const auto &chunk : part.chunks) {
    chunks.push_back({
        {"n", chunk.chunkNumber},
        {"plain_size", chunk.plaintextSize},
        {"cipher_size", chunk.ciphertextSize},
        {"hash", chunk.encryptedHash},
    });
  }

  return UploadResumePartRecord{
      .partNumber = static_cast<int>(part.plan.partNumber),
      .etag = part.etag,
      .uploadHash = part.uploadHash,
      .chunkManifestJson = chunks.dump(),
      .combinedChunkHashes = encodeBase64(part.combinedChunkHashes),
  };
}

UploadedPartResult readUploadedPartResult(const UploadResumePartRecord &record,
                                          const UploadPlan &plan) {
  UploadedPartResult part;
  part.plan = makePartPlan(static_cast<uint64_t>(record.partNumber), plan);
  part.uploadHash = record.uploadHash;
  part.etag = record.etag;
  part.combinedChunkHashes = decodeBase64(record.combinedChunkHashes);

  const nlohmann::json chunks = nlohmann::json::parse(record.chunkManifestJson);
  if (!chunks.is_array()) {
    throw std::runtime_error("resume chunk manifest must be an array");
  }

  for (const auto &chunk : chunks) {
    part.chunks.push_back(EncryptedChunkResult{
        .chunkNumber = chunk.at("n").get<uint64_t>(),
        .plaintextSize = chunk.at("plain_size").get<uint64_t>(),
        .ciphertextSize = chunk.at("cipher_size").get<uint64_t>(),
        .encryptedHash = chunk.at("hash").get<std::string>(),
    });
  }

  return part;
}

StartUploadResponse
startResponseFromSession(const UploadResumeSessionRecord &session) {
  return StartUploadResponse{
      .fileId = session.fileId,
      .vaultId = session.vaultId,
      .uploadSessionId = session.uploadSessionId,
      .providerUploadId = session.providerUploadId,
      .fileChunkSize = session.fileChunkSize,
      .totalChunks = session.totalChunks,
      .uploadPartSize = session.uploadPartSize,
      .uploadPartCount = session.uploadPartCount,
      .uploadUrl = "",
  };
}

void zeroizeArtifacts(FileEncryptor &fileEncryptor,
                      UploadArtifacts &artifacts) {
  if (!artifacts.fileKey.empty()) {
    fileEncryptor.zeroize(artifacts.fileKey);
  }
  if (!artifacts.encryptedFileKey.empty()) {
    fileEncryptor.zeroize(artifacts.encryptedFileKey);
  }
  if (!artifacts.encryptedMetadata.empty()) {
    fileEncryptor.zeroize(artifacts.encryptedMetadata);
  }
  if (!artifacts.encryptedManifest.empty()) {
    fileEncryptor.zeroize(artifacts.encryptedManifest);
  }
  if (!artifacts.encryptedHash.empty()) {
    fileEncryptor.zeroize(artifacts.encryptedHash);
  }
}

} // namespace

UploadService::UploadService(ArkiveApi &api, FileEncryptor &fileEncryptor,
                             UploadResumeRepo &uploadResumeRepo)
    : api_(api), fileEncryptor_(fileEncryptor),
      uploadResumeRepo_(uploadResumeRepo),
      multipartUploader_(api, fileEncryptor),
      uploadFinalizer_(api, fileEncryptor) {}

UploadLimitsResponse UploadService::uploadLimits() {
  std::call_once(uploadLimitsOnce_, [this]() {
    cachedUploadLimits_ = api_.uploadLimits();
  });
  return cachedUploadLimits_;
}

UploadFileResponse UploadService::uploadFile(const std::filesystem::path &path,
                                             const Entry &entry) {
  if (entry.isDirectory) {
    throw std::invalid_argument("uploadFile does not support directories");
  }
  if (!entry.size.has_value() || *entry.size < 0) {
    throw std::invalid_argument("uploadFile requires a valid local file size");
  }

  const std::string localPath = normalizeFsPath(path);
  const uint64_t originalSize = static_cast<uint64_t>(*entry.size);
  const UploadPlan uploadPlan = UploadPlanner::createPlan(originalSize);
  spdlog::info("Uploading file={} mode={}", path.string(),
               uploadPlan.totalChunks == 1 ? "single-part" : "multipart");
  const std::optional<std::string> localMtime = currentMtimeString(path);
  const UploadLimitsResponse limits = uploadLimits();
  const uint64_t partConcurrency = ArkiveUploadPolicy::resolvePartConcurrency(
      uploadPlan.uploadPartCount, limits.partConcurrency);

  for (int attempt = 0; attempt < 2; ++attempt) {
    UploadArtifacts artifacts;
    StartUploadResponse started;
    bool usedResumeSession = false;

    try {
      std::vector<UploadedPartResult> completedParts;

      if (const auto resumeSession =
              uploadResumeRepo_.getSessionByLocalPath(localPath);
          resumeSession.has_value()) {
        if (sessionMatchesEntry(*resumeSession, entry, localPath, originalSize,
                                localMtime, uploadPlan)) {
          usedResumeSession = true;
          started = startResponseFromSession(*resumeSession);
          completedParts.reserve(
              static_cast<size_t>(uploadPlan.uploadPartCount));
          for (const auto &part :
               uploadResumeRepo_.listParts(resumeSession->uploadSessionId)) {
            completedParts.push_back(readUploadedPartResult(part, uploadPlan));
          }

          std::vector<uint8_t> encryptedFileKeyBlob =
              decodeBase64(resumeSession->encryptedFileKeyBlob);
          artifacts.fileKey = fileEncryptor_.decryptResumeFileKey(
              encryptedFileKeyBlob, resumeSession->uploadSessionId);
          if (!encryptedFileKeyBlob.empty()) {
            fileEncryptor_.zeroize(encryptedFileKeyBlob);
          }
        } else {
          uploadResumeRepo_.deleteSessionByLocalPath(localPath);
        }
      }

      if (!usedResumeSession) {
        started = api_.startUpload(StartUploadRequest{
            .originalSize = static_cast<int64_t>(uploadPlan.originalSize),
            .fileChunkSize = static_cast<int64_t>(uploadPlan.fileChunkSize),
            .totalChunks = static_cast<int>(uploadPlan.totalChunks),
            .uploadPartSize = static_cast<int64_t>(uploadPlan.uploadPartSize),
            .uploadPartCount = static_cast<int>(uploadPlan.uploadPartCount),
            .encryptionVersion = 1,
            .folderId = entry.parentFolderId,
            .singlePart = uploadPlan.totalChunks == 1,
        });

        artifacts.fileKey = fileEncryptor_.createFileKey();
        std::vector<uint8_t> encryptedFileKeyBlob =
            fileEncryptor_.encryptResumeFileKey(artifacts.fileKey,
                                                started.uploadSessionId);
        uploadResumeRepo_.replaceSession(UploadResumeSessionRecord{
            .id = started.uploadSessionId,
            .entryId = entry.id,
            .localPath = localPath,
            .localSize = static_cast<int64_t>(originalSize),
            .localMtime = localMtime,
            .localHash = entry.contentHash,
            .folderId = entry.parentFolderId,
            .vaultId = started.vaultId,
            .fileId = started.fileId,
            .uploadSessionId = started.uploadSessionId,
            .providerUploadId = started.providerUploadId,
            .fileChunkSize = started.fileChunkSize,
            .totalChunks = started.totalChunks,
            .uploadPartSize = started.uploadPartSize,
            .uploadPartCount = started.uploadPartCount,
            .encryptedFileKeyBlob = encodeBase64(encryptedFileKeyBlob),
        });
        if (!encryptedFileKeyBlob.empty()) {
          fileEncryptor_.zeroize(encryptedFileKeyBlob);
        }
      }

      const std::vector<UploadedPartResult> uploadedParts =
          multipartUploader_.uploadParts(
              path, artifacts.fileKey, started, uploadPlan, partConcurrency,
              completedParts, [this, &started](const UploadedPartResult &part) {
                uploadResumeRepo_.upsertPart(started.uploadSessionId,
                                             makeResumePartRecord(part));
              });
      artifacts = uploadFinalizer_.completeUpload(
          path, entry, started, uploadPlan, uploadedParts, artifacts.fileKey);
      uploadResumeRepo_.deleteSessionByUploadSessionId(started.uploadSessionId);

      zeroizeArtifacts(fileEncryptor_, artifacts);
      return UploadFileResponse{
          .fileId = started.fileId,
          .vaultId = started.vaultId,
          .uploadSessionId = started.uploadSessionId,
          .providerUploadId = started.providerUploadId,
      };
    } catch (const HttpError &error) {
      zeroizeArtifacts(fileEncryptor_, artifacts);
      if (attempt == 0 && usedResumeSession &&
          shouldResetResumeSession(error)) {
        uploadResumeRepo_.deleteSessionByLocalPath(localPath);
        continue;
      }
      throw;
    } catch (...) {
      zeroizeArtifacts(fileEncryptor_, artifacts);
      throw;
    }
  }

  throw std::runtime_error("upload retry state exhausted");
}
