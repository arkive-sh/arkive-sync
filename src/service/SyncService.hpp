#pragma once

#include "fs/FileScanner.hpp"
#include "repo/QueueRepo.hpp"
#include "repo/SyncRepo.hpp"

class SyncService {
public:
  SyncService(SyncRepo &syncRepo, QueueRepo &queueRepo,
              FileScanner &fileScanner);

  void addPath();
  size_t scanRoot();

private:
  SyncRepo &syncRepo_;
  QueueRepo &queueRepo_;
  FileScanner &fileScanner_;
};
