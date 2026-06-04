#pragma once

#include "crypto/RustCrypto.hpp"
#include "repo/SyncRepo.hpp"
#include <filesystem>

class SyncService {
public:
  SyncService(SyncRepo &syncRepo, RustCrypto &crypto);

  void addPath(const std::filesystem::path &rootPath);
  size_t scanPath(const std::string &rootId,
                  const std::filesystem::path &absolutePath);
  size_t scanRoot(const std::filesystem::path &rootPath);

private:
  SyncRepo &syncRepo_;
  RustCrypto &crypto_;
};
