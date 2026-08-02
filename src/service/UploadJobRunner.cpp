#include "service/UploadJobRunner.hpp"

#include "repo/SyncRepo.hpp"
#include "service/UploadService.hpp"
#include "sync/SyncPolicy.hpp"
#include "sync/SyncStateClassifier.hpp"

#include <chrono>
#include <filesystem>
#include <optional>
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

} // namespace

StaleUploadError::StaleUploadError(std::string message,
                                   std::string syncRootPath)
    : std::runtime_error(std::move(message)),
      syncRootPath_(std::move(syncRootPath)) {}

const std::string &StaleUploadError::syncRootPath() const {
  return syncRootPath_;
}

UploadJobRunner::UploadJobRunner(SyncRepo &syncRepo, EntryRepo &entryRepo,
                                 RemoteEntryRepo &remoteEntryRepo,
                                 IUploadService &uploadService)
    : syncRepo_(syncRepo), entryRepo_(entryRepo),
      remoteEntryRepo_(remoteEntryRepo),
      uploadService_(uploadService) {}

void UploadJobRunner::run(const TransferJob &job) {
  if (job.jobType != "upload_file") {
    throw std::invalid_argument("UploadJobRunner requires upload_file job");
  }

  const auto entry = entryRepo_.getEntryById(job.entryId);
  if (!entry.has_value()) {
    throw std::runtime_error("Queued upload entry is missing");
  }

  const auto syncRoot = syncRepo_.findSyncRootById(entry->syncRootId);
  if (!syncRoot.has_value()) {
    throw std::runtime_error("Queued upload sync root is missing");
  }

  if (SyncPolicy::decide(SyncStateClassifier::classify(*entry),
                         syncRoot->mode) != SyncDecision::Upload) {
    return;
  }

  const std::filesystem::path absolutePath =
      std::filesystem::path(syncRoot->localPath) / entry->relativePath;
  const std::filesystem::path parentPath =
      std::filesystem::path(entry->relativePath).parent_path();

  if (!std::filesystem::exists(absolutePath)) {
    throw std::runtime_error("Queued upload file is missing");
  }
  if (!std::filesystem::is_regular_file(absolutePath)) {
    throw std::runtime_error("Queued upload path is not a regular file");
  }

  if (entry->size.has_value()) {
    std::error_code error;
    const auto currentSize = std::filesystem::file_size(absolutePath, error);
    if (error || currentSize != static_cast<uint64_t>(*entry->size)) {
      throw StaleUploadError(
          "Queued upload file size changed since the last sync scan",
          syncRoot->localPath);
    }
  }

  if (entry->mtime.has_value()) {
    const auto currentMtime = currentMtimeString(absolutePath);
    if (!currentMtime.has_value() ||
        *currentMtime != toMtimeString(*entry->mtime)) {
      throw StaleUploadError(
          "Queued upload file mtime changed since the last sync scan",
          syncRoot->localPath);
    }
  }

  std::optional<std::string> remoteParentFolderId;
  if (parentPath.empty()) {
    if (!syncRoot->folderId.empty()) {
      remoteParentFolderId = syncRoot->folderId;
    } else {
      remoteParentFolderId = std::nullopt;
    }
  } else {
    const auto parentEntry = entryRepo_.findEntryByPath(
        entry->syncRootId, parentPath.generic_string());
    if (!parentEntry.has_value() || !parentEntry->remoteId.has_value()) {
      throw std::runtime_error("Queued upload parent folder has no remote_id");
    }
    remoteParentFolderId = parentEntry->remoteId;
  }

  Entry uploadEntry = *entry;
  uploadEntry.parentFolderId = remoteParentFolderId;

  const UploadFileResponse uploaded =
      uploadService_.uploadFile(absolutePath, uploadEntry);
  remoteEntryRepo_.markEntryUploaded(job.entryId, uploaded.fileId,
                                     remoteParentFolderId);
}
