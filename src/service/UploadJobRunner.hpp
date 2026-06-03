#pragma once

#include "repo/QueueRepo.hpp"

class RustCrypto;
class SyncRepo;
class UploadService;

class UploadJobRunner {
public:
  UploadJobRunner(SyncRepo &syncRepo, UploadService &uploadService,
                  RustCrypto &crypto);

  void run(const TransferJob &job);

private:
  SyncRepo &syncRepo_;
  UploadService &uploadService_;
  RustCrypto &crypto_;
};
