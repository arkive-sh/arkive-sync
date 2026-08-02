#pragma once

#include "repo/QueueRepo.hpp"

class ArkiveApi;
class EntryRepo;
class FileEncryptor;
class RemoteEntryRepo;
class SyncRepo;
class UserRepo;

class FolderCreateWorker {
public:
  FolderCreateWorker(SyncRepo &syncRepo, EntryRepo &entryRepo,
                     RemoteEntryRepo &remoteEntryRepo, UserRepo &userRepo,
                     FileEncryptor &fileEncryptor, ArkiveApi &api);

  bool ensureRootFolder(const std::string &syncRootId);
  void run(const TransferJob &job);

private:
  std::string userId() const;

  SyncRepo &syncRepo_;
  EntryRepo &entryRepo_;
  RemoteEntryRepo &remoteEntryRepo_;
  UserRepo &userRepo_;
  FileEncryptor &fileEncryptor_;
  ArkiveApi &api_;
};
