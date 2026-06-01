#pragma once

#include "fs/FileScanner.hpp"
#include "repo/SyncRepo.hpp"

class SyncService {
public:
  SyncService(SyncRepo &syncRepo, FileScanner &fileScanner);

  void addPath();
  size_t scanRoot();

private:
  SyncRepo &syncRepo_;
  FileScanner &fileScanner_;
};
