#pragma once

#include "crypto/RustCrypto.hpp"
#include "repo/QueueRepo.hpp"
#include "repo/SyncRepo.hpp"
#include <filesystem>

class SyncService {
public:
  SyncService(SyncRepo &syncRepo, QueueRepo &queueRepo, RustCrypto &crypto);

  void addPath(const std::filesystem::path &rootPath);
  size_t scanRoot(const std::filesystem::path &rootPath);

private:
  SyncRepo &syncRepo_;
  QueueRepo &queueRepo_;
  RustCrypto &crypto_;
};
