#pragma once

#include "repo/EntryRepo.hpp"
#include "repo/QueueRepo.hpp"

#include <stdexcept>
#include <string>

class StaleUploadError : public std::runtime_error {
public:
  StaleUploadError(std::string message, std::string syncRootPath);

  const std::string &syncRootPath() const;

private:
  std::string syncRootPath_;
};

class RustCrypto;
class SyncRepo;
class IUploadService;

class UploadJobRunner {
public:
  UploadJobRunner(SyncRepo &syncRepo, EntryRepo &entryRepo,
                  IUploadService &uploadService);

  void run(const TransferJob &job);

private:
  SyncRepo &syncRepo_;
  EntryRepo &entryRepo_;
  IUploadService &uploadService_;
};
