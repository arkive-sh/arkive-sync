#pragma once

#include "repo/QueueRepo.hpp"

class QueueService {
public:
  explicit QueueService(QueueRepo &queueRepo);

  QueueStats stats();
  size_t processQueuedUploads();
  std::optional<TransferJob> claimNextQueuedUpload();
  void markDone(const std::string &jobId);
  void markFailed(const std::string &jobId, const std::string &errorMessage);
  void incrementProgress(const std::string &jobId, uint64_t bytesDone);
  void retryFailed();
  void clearDone();

private:
  QueueRepo &queueRepo_;
};
