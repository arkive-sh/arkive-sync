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
                                  bool enabled) {
  const std::string normalizedPath = normalizeFsPath(localPath);

  if (!std::filesystem::is_directory(normalizedPath)) {
    throw std::invalid_argument("Path needs to be a folder");
  }

  const std::string pathHash = FileHasher(normalizedPath, crypto_).hashPath();

  SyncRoot root{
      .Id = makeRootId(pathHash),
      .localPath = normalizedPath,
      .localHash = pathHash,
      .folderId = folderId.value_or(""),
      .enabled = enabled ? 1 : 0,
      .createdAt = "",
  };

  syncRepo_.upsertSyncRoot(root);
  return root;
}
