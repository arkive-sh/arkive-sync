#pragma once

#include "api/ArkiveApi.hpp"
#include "repo/QueueRepo.hpp"
#include "repo/SyncRepo.hpp"

class UploadJobRunner;

class QueueService {
public:
  QueueService(QueueRepo &queueRepo, SyncRepo &syncRepo,
               UploadJobRunner &uploadJobRunner, ArkiveApi &api);

  QueueStats stats();
  std::optional<TransferJob> claimNextQueuedUpload();
  void markDone(const std::string &jobId);
  void markFailed(const std::string &jobId, const std::string &errorMessage);
  void incrementProgress(const std::string &jobId, uint64_t bytesDone);
  void retryFailed();
  void clearDone();
  void processQueuedUploads();

private:
  size_t fillUploadQueue();

  QueueRepo &queueRepo_;
  SyncRepo &syncRepo_;
  UploadJobRunner &uploadJobRunner_;
  ArkiveApi &api_;
};
