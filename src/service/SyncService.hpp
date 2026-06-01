#pragma once

#include "repo/QueueRepo.hpp"
#include "repo/SyncRepo.hpp"
#include <filesystem>

class SyncService {
public:
  SyncService(SyncRepo &syncRepo, QueueRepo &queueRepo);

  void addPath(const std::filesystem::path &rootPath);
  size_t scanRoot(const std::filesystem::path &rootPath);

private:
  SyncRepo &syncRepo_;
  QueueRepo &queueRepo_;
};
