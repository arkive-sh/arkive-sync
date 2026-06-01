#pragma once

#include "fs/FileScanner.hpp"
#include "repo/SyncRepo.hpp"

class SyncService {
public:
  SyncService(SyncRepo &syncRepo, FileScanner &fileScanner);

  size_t addPath();

private:
  SyncRepo &syncRepo_;
  FileScanner &fileScanner_;
};
