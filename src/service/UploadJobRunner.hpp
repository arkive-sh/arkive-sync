#pragma once

#include "repo/QueueRepo.hpp"

class RustCrypto;
class SyncRepo;
class IUploadService;

class UploadJobRunner {
public:
  UploadJobRunner(SyncRepo &syncRepo, IUploadService &uploadService,
                  RustCrypto &crypto);

  void run(const TransferJob &job);

private:
  SyncRepo &syncRepo_;
  IUploadService &uploadService_;
  RustCrypto &crypto_;
};
