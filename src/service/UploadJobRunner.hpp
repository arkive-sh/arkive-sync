#pragma once

#include "repo/EntryRepo.hpp"
#include "repo/QueueRepo.hpp"
#include "repo/RemoteEntryRepo.hpp"
#include "service/UploadService.hpp"

#include <stdexcept>
#include <optional>
#include <string>
#include <vector>

class StaleUploadError : public std::runtime_error {
public:
  StaleUploadError(std::string message, std::string syncRootPath);

  const std::string &syncRootPath() const;

private:
  std::string syncRootPath_;
};

class RustCrypto;
class SyncRepo;

struct UploadJobBatchResult {
  TransferJob job;
  std::string error;
};

class UploadJobRunner {
public:
  UploadJobRunner(SyncRepo &syncRepo, EntryRepo &entryRepo,
                  RemoteEntryRepo &remoteEntryRepo,
                  IUploadService &uploadService);

  void run(const TransferJob &job);
  std::vector<UploadJobBatchResult>
  runBatch(const std::vector<TransferJob> &jobs);

private:
  std::optional<UploadBatchItem>
  prepareUploadItem(const TransferJob &job);

  SyncRepo &syncRepo_;
  EntryRepo &entryRepo_;
  RemoteEntryRepo &remoteEntryRepo_;
  IUploadService &uploadService_;
};
