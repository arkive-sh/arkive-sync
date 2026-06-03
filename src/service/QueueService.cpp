#include "service/QueueService.hpp"
#include "api/ArkiveHttpClient.hpp"
#include "repo/QueueRepo.hpp"
#include "repo/SyncRepo.hpp"
#include "service/UploadService.hpp"
#include <filesystem>
#include "spdlog/spdlog.h"
#include <exception>

namespace {

bool isUploadQueueLimitError(const std::exception &ex) {
  const auto *httpError = dynamic_cast<const HttpError *>(&ex);
  return httpError != nullptr &&
         httpError->apiError() == "upload queue limit reached";
}

} // namespace

QueueService::QueueService(QueueRepo &queueRepo, SyncRepo &syncRepo,
                           UploadService &uploadService, ArkiveApi &api)
    : queueRepo_(queueRepo), syncRepo_(syncRepo),
      uploadService_(uploadService), api_(api) {}

QueueStats QueueService::stats() { return queueRepo_.stats(); }

std::optional<TransferJob> QueueService::claimNextQueuedUpload() {
  return queueRepo_.claimNextQueuedUpload();
}

void QueueService::processQueuedUploads() {
  while (true) {
    fillUploadQueue();

    std::optional<TransferJob> queuedJob = queueRepo_.claimNextQueuedUpload();
    if (!queuedJob.has_value()) {
      break;
    }

    try {
      const auto entry = syncRepo_.getEntryById(queuedJob->entryId);
      if (!entry.has_value()) {
        throw std::runtime_error("Queued upload entry is missing");
      }

      const auto syncRoot = syncRepo_.getSyncRootById(entry->syncRootId);
      if (!syncRoot.has_value()) {
        throw std::runtime_error("Queued upload sync root is missing");
      }

      const std::filesystem::path absolutePath =
          std::filesystem::path(syncRoot->localPath) / entry->localPath;

      uploadService_.uploadFile(absolutePath, *entry);
      spdlog::info("Uploaded file: {}", absolutePath.string());

      queueRepo_.markDone(queuedJob->id);
      syncRepo_.markEntrySynced(queuedJob->entryId);
    } catch (const std::exception &ex) {
      queueRepo_.markFailed(queuedJob->id, ex.what());
      if (isUploadQueueLimitError(ex)) {
        spdlog::warn(
            "Stopped queue processing because the server upload queue limit was reached");
        break;
      }
    } catch (...) {
      queueRepo_.markFailed(queuedJob->id, "Unknown queue processing error");
    }
  }
}

void QueueService::markDone(const std::string &jobId) {
  queueRepo_.markDone(jobId);
}

void QueueService::markFailed(const std::string &jobId,
                              const std::string &errorMessage) {
  queueRepo_.markFailed(jobId, errorMessage);
}

void QueueService::incrementProgress(const std::string &jobId,
                                     uint64_t bytesDone) {
  queueRepo_.incrementProgress(jobId, bytesDone);
}

void QueueService::retryFailed() { queueRepo_.retryFailed(); }

void QueueService::clearDone() { queueRepo_.clearDone(); }

size_t QueueService::fillUploadQueue() {
  const UploadLimitsResponse limits = api_.uploadLimits();
  if (limits.maxQueueItems <= 0) {
    return 0;
  }

  const QueueStats currentStats = queueRepo_.stats();
  const int activeQueueItems = currentStats.queued + currentStats.running;
  // Desktop sync may discover far more files than the web-facing queue cap.
  // Keep refilling only up to the server limit so large sync trees drain in
  // batches instead of overflowing the server's active upload window.
  const int availableSlots = limits.maxQueueItems - activeQueueItems;
  if (availableSlots <= 0) {
    return 0;
  }

  const auto pendingEntries =
      syncRepo_.listPendingUploadEntries(static_cast<size_t>(availableSlots));
  for (const auto &entry : pendingEntries) {
    queueRepo_.enqueueUpload(entry.id, entry.localPath, entry.parentFolderId,
                             static_cast<uint64_t>(entry.localSize.value_or(0)));
  }

  return pendingEntries.size();
}
