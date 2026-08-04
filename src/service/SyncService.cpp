#include "service/SyncService.hpp"

#include "fs/FileHasher.hpp"
#include "fs/helpers/PathHelpers.hpp"

SyncService::SyncService(SyncRepo &syncRepo, RustCrypto &crypto)
    : syncRepo_(syncRepo), crypto_(crypto) {}

std::string SyncService::makeRootId(const std::string &pathHash) const {
  return "sync-root-" + pathHash;
}

SyncRoot SyncService::addSyncRoot(const std::filesystem::path &localPath,
                                  std::optional<std::string> folderId,
                                  bool enabled, SyncMode mode) {
  const std::string normalizedPath = normalizeFsPath(localPath);

  if (!std::filesystem::is_directory(normalizedPath)) {
    throw std::invalid_argument("Path needs to be a folder");
  }

  const std::string pathHash = FileHasher(normalizedPath, crypto_).hashPath();

  SyncRoot root{
      .Id = makeRootId(pathHash),
      .localPath = normalizedPath,
      .folderId = folderId.value_or(""),
      .enabled = enabled ? 1 : 0,
      .mode = mode,
      .createdAt = "",
  };

  syncRepo_.upsertSyncRoot(root);
  return root;
}

std::optional<SyncRoot>
SyncService::findSyncRootById(const std::string &syncRootId) {
  return syncRepo_.findSyncRootById(syncRootId);
}

std::vector<SyncRoot> SyncService::getSyncRoots() { return syncRepo_.getSyncRoots(); }

void SyncService::removeSyncRoot(const std::string &syncRootId) {
  if (!syncRepo_.findSyncRootById(syncRootId).has_value()) {
    throw std::invalid_argument("Sync root not found: " + syncRootId);
  }
  syncRepo_.disableSyncRoot(syncRootId);
}
