#pragma once

#include "repo/QueueRepo.hpp"

class ArkiveApi;
class EntryRepo;
class FileEncryptor;
class SyncRepo;
class UserRepo;

class FolderCreateWorker {
public:
  FolderCreateWorker(SyncRepo &syncRepo, EntryRepo &entryRepo,
                     UserRepo &userRepo, FileEncryptor &fileEncryptor,
                     ArkiveApi &api);

  bool ensureRootFolder(const std::string &syncRootId);
  void run(const TransferJob &job);

private:
  std::string userId() const;

  SyncRepo &syncRepo_;
  EntryRepo &entryRepo_;
  UserRepo &userRepo_;
  FileEncryptor &fileEncryptor_;
  ArkiveApi &api_;
};
