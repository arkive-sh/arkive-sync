#pragma once

#include "crypto/RustCrypto.hpp"
#include "repo/SyncRepo.hpp"

#include <filesystem>
#include <optional>
#include <string>

class SyncService {
public:
  SyncService(SyncRepo &syncRepo, RustCrypto &crypto);

  SyncRoot addSyncRoot(const std::filesystem::path &localPath,
                       std::optional<std::string> folderId = std::nullopt,
                       bool enabled = true,
                       SyncMode mode = SyncMode::TwoWay);
  std::optional<SyncRoot> findSyncRootById(const std::string &syncRootId);
  std::vector<SyncRoot> getSyncRoots();
  void removeSyncRoot(const std::string &syncRootId);

private:
  std::string makeRootId(const std::string &pathHash) const;

  SyncRepo &syncRepo_;
  RustCrypto &crypto_;
};
