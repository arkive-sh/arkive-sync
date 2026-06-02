#pragma once

#include "repo/QueueRepo.hpp"
#include "repo/SyncRepo.hpp"

class UploadService;

class QueueService {
public:
  QueueService(QueueRepo &queueRepo, SyncRepo &syncRepo,
               UploadService &uploadService);

  QueueStats stats();
  std::optional<TransferJob> claimNextQueuedUpload();
  void markDone(const std::string &jobId);
  void markFailed(const std::string &jobId, const std::string &errorMessage);
  void incrementProgress(const std::string &jobId, uint64_t bytesDone);
  void retryFailed();
  void clearDone();
  void processQueuedUploads();

private:
  QueueRepo &queueRepo_;
  SyncRepo &syncRepo_;
  UploadService &uploadService_;
};
