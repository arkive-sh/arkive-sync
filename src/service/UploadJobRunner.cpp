#include "service/UploadJobRunner.hpp"

#include "helpers/PathCodec.hpp"
#include "repo/SyncRepo.hpp"
#include "service/UploadService.hpp"

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

StaleUploadError::StaleUploadError(std::string message, std::string syncRootPath)
    : std::runtime_error(std::move(message)),
      syncRootPath_(std::move(syncRootPath)) {}

const std::string &StaleUploadError::syncRootPath() const { return syncRootPath_; }

UploadJobRunner::UploadJobRunner(SyncRepo &syncRepo,
                                 IUploadService &uploadService)
    : syncRepo_(syncRepo), uploadService_(uploadService) {}

void UploadJobRunner::run(const TransferJob &job) {
  const auto entry = syncRepo_.local().getEntryById(job.entryId);
  if (!entry.has_value()) {
    throw std::runtime_error("Queued upload entry is missing");
  }

  const auto syncRoot = syncRepo_.roots().getSyncRootById(entry->syncRootId);
  if (!syncRoot.has_value()) {
    throw std::runtime_error("Queued upload sync root is missing");
  }

  const std::filesystem::path absolutePath =
      PathCodec::joinRoot(syncRoot->localPath, entry->localPath);

  if (!std::filesystem::exists(absolutePath)) {
    throw std::runtime_error("Queued upload file is missing");
  }
  if (!std::filesystem::is_regular_file(absolutePath)) {
    throw std::runtime_error("Queued upload path is not a regular file");
  }

  if (entry->localSize.has_value()) {
    std::error_code error;
    const auto currentSize = std::filesystem::file_size(absolutePath, error);
    if (error || currentSize != static_cast<uint64_t>(*entry->localSize)) {
      throw StaleUploadError(
          "Queued upload file size changed since the last sync scan",
          syncRoot->localPath);
    }
  }

  if (entry->localMtime.has_value()) {
    const auto currentMtime = currentMtimeString(absolutePath);
    if (!currentMtime.has_value() || currentMtime != entry->localMtime) {
      throw StaleUploadError(
          "Queued upload file mtime changed since the last sync scan",
          syncRoot->localPath);
    }
  }

  const UploadFileResponse uploaded = uploadService_.uploadFile(absolutePath, *entry);
  syncRepo_.local().markEntryUploaded(job.entryId, uploaded.fileId);
}
