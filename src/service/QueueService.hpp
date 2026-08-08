#pragma once

#include <optional>
#include <string>
#include <vector>

struct Entry;
struct SyncRoot;

struct QueueBuildPage {
  std::optional<std::string> nextPath;
  bool complete = false;
};

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
  QueueBuildPage buildPage(
      const std::string &syncRootId,
      const std::optional<std::string> &afterPath, bool scanComplete);
  void enqueueEntry(const std::string &syncRootId,
                    const std::string &relativePath);
  void runTick();

private:
  void enqueueEntry(const SyncRoot &syncRoot, const Entry &entry);

  EntryRepo &entryRepo_;
  QueueRepo &queueRepo_;
  SyncRepo &syncRepo_;
  FolderCreateWorker *folderCreateWorker_;
  UploadJobRunner *uploadJobRunner_;
};
