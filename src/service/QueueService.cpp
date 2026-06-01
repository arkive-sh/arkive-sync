#include "service/QueueService.hpp"

QueueService::QueueService(QueueRepo &queueRepo) : queueRepo_(queueRepo) {}

QueueStats QueueService::stats() { return queueRepo_.stats(); }

std::optional<TransferJob> QueueService::claimNextQueuedUpload() {
  return queueRepo_.claimNextQueuedUpload();
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
