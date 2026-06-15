#pragma once

#include "repo/QueueRepo.hpp"

class ArkiveApi;
class EntryRepo;
class FileEncryptor;
class SyncRepo;

class FolderCreateWorker {
public:
  FolderCreateWorker(SyncRepo &syncRepo, EntryRepo &entryRepo,
                     FileEncryptor &fileEncryptor, ArkiveApi &api);

  void run(const TransferJob &job);

private:
  SyncRepo &syncRepo_;
  EntryRepo &entryRepo_;
  FileEncryptor &fileEncryptor_;
  ArkiveApi &api_;
};
