#pragma once

#include <cstdint>
#include <optional>
#include <sqlite3.h>
#include <string>

struct QueueStats {
  int queued = 0;
  int running = 0;
  int failed = 0;
  int done = 0;
};

struct TransferJob {
  std::string id;
  std::string entryId;
  std::string direction;
  std::string status;
  std::string localPath;
  std::optional<std::string> remoteId;
  std::optional<std::string> remoteFolderId;
  uint64_t bytesTotal = 0;
  uint64_t bytesDone = 0;
  int retryCount = 0;
};

class QueueRepo {
public:
  explicit QueueRepo(sqlite3 *db);

  bool hasActiveUploadForEntry(const std::string &entryId);

  void enqueueUpload(const std::string &entryId, const std::string &localPath,
                     const std::optional<std::string> &remoteFolderId,
                     uint64_t bytesTotal);

  QueueStats stats();

  std::optional<TransferJob> claimNextQueuedUpload();

  void markDone(const std::string &jobId);

  void markFailed(const std::string &jobId, const std::string &errorMessage);
  void retryJob(const std::string &jobId);

  void incrementProgress(const std::string &jobId, uint64_t bytesDone);

  void retryFailed();

  void clearDone();

private:
  sqlite3 *db_;
};
