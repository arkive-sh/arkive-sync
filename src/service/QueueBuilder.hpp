#pragma once

#include <string>

class EntryRepo;
class QueueRepo;
class SyncRepo;

class QueueBuilder {
public:
  QueueBuilder(EntryRepo &entryRepo, QueueRepo &queueRepo, SyncRepo &syncRepo);

  void build(const std::string &syncRootId);

private:
  EntryRepo &entryRepo_;
  QueueRepo &queueRepo_;
  SyncRepo &syncRepo_;
};
