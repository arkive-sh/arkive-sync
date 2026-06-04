#include "service/QueueService.hpp"
#include "api/HttpError.hpp"
#include "repo/QueueRepo.hpp"
#include "repo/SyncRepo.hpp"
#include "service/SyncService.hpp"
#include "service/UploadJobRunner.hpp"
#include "spdlog/spdlog.h"
#include <exception>

namespace {

bool isRetryableHttpError(const HttpError &error) {
  return error.statusCode == 429 || error.statusCode >= 500;
}

bool isUploadQueueLimitError(const HttpError &error) {
  return error.apiError() == "upload queue limit reached";
}

} // namespace

QueueService::QueueService(QueueRepo &queueRepo, SyncRepo &syncRepo,
                           UploadJobRunner &uploadJobRunner,
                           SyncService &syncService, ArkiveApi &api)
    : queueRepo_(queueRepo), syncRepo_(syncRepo),
      uploadJobRunner_(uploadJobRunner), syncService_(syncService), api_(api) {}

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
      uploadJobRunner_.run(*queuedJob);
      spdlog::info("Uploaded queued file for entry: {}", queuedJob->entryId);

      queueRepo_.markDone(queuedJob->id);
    } catch (const StaleUploadError &error) {
      queueRepo_.retryJob(queuedJob->id);
      syncService_.scanRoot(error.syncRootPath());
      spdlog::warn("Requeued stale upload job {} and rescanned root {}",
                   queuedJob->id, error.syncRootPath());
    } catch (const HttpError &error) {
      if (isUploadQueueLimitError(error)) {
        queueRepo_.retryJob(queuedJob->id);
        spdlog::warn(
            "Stopped queue processing because the server upload queue limit was reached");
        break;
      }

      if (isRetryableHttpError(error)) {
        queueRepo_.retryJob(queuedJob->id);
        spdlog::warn("Requeued upload job {} after retryable HTTP {}",
                     queuedJob->id, error.statusCode);
        break;
      }

      queueRepo_.markFailed(queuedJob->id, error.what());
    } catch (const std::exception &ex) {
      queueRepo_.markFailed(queuedJob->id, ex.what());
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
    queueRepo_.enqueueUpload(entry.id, "", entry.parentFolderId,
                             static_cast<uint64_t>(entry.localSize.value_or(0)));
  }

  return pendingEntries.size();
}
