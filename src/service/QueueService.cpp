#include "service/QueueService.hpp"
#include "repo/QueueRepo.hpp"
#include "repo/SyncRepo.hpp"
#include "spdlog/spdlog.h"
#include <exception>

QueueService::QueueService(QueueRepo &queueRepo, SyncRepo &syncRepo)
    : queueRepo_(queueRepo), syncRepo_(syncRepo) {}

QueueStats QueueService::stats() { return queueRepo_.stats(); }

std::optional<TransferJob> QueueService::claimNextQueuedUpload() {
  return queueRepo_.claimNextQueuedUpload();
}

void QueueService::processQueuedUploads() {
  while (true) {
    std::optional<TransferJob> queuedJob = queueRepo_.claimNextQueuedUpload();
    if (!queuedJob.has_value()) {
      break;
    }

    try {
      // TODO: replace fake upload with real upload worker logic.
      spdlog::info("file uploaded");

      queueRepo_.markDone(queuedJob->id);
      syncRepo_.markEntrySynced(queuedJob->entryId);
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
