#pragma once

#include <string>

class EntryRepo;
class QueueRepo;
class SyncRepo;
class FolderCreateWorker;
class UploadJobRunner;

class QueueService {
public:
  QueueService(EntryRepo &entryRepo, QueueRepo &queueRepo, SyncRepo &syncRepo,
               FolderCreateWorker *folderCreateWorker,
               UploadJobRunner *uploadJobRunner);

  void build(const std::string &syncRootId);
  void runTick();

private:
  EntryRepo &entryRepo_;
  QueueRepo &queueRepo_;
  SyncRepo &syncRepo_;
  FolderCreateWorker *folderCreateWorker_;
  UploadJobRunner *uploadJobRunner_;
};
