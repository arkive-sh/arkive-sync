#include "sync/RemoteScanner.hpp"

#include "api/ArkiveApi.hpp"
#include "repo/EntryRepo.hpp"
#include "repo/SyncRepo.hpp"

#include <stdexcept>

RemoteScanner::RemoteScanner(SyncRepo &syncRepo, EntryRepo &entryRepo,
                             ArkiveApi &api)
    : syncRepo_(syncRepo), entryRepo_(entryRepo), api_(api) {}

void RemoteScanner::scanRoot(const std::string &syncRootId) const {
  const auto syncRoot = syncRepo_.findSyncRootById(syncRootId);
  if (!syncRoot.has_value()) {
    throw std::runtime_error("Sync root is missing");
  }

  const std::optional<std::string> rootRemoteFolderId =
      syncRoot->folderId.empty()
          ? std::nullopt
          : std::optional<std::string>(syncRoot->folderId);
  scanFolder(syncRootId, rootRemoteFolderId);
}

ListSyncEntriesResponse
RemoteScanner::fetchEntries(const std::optional<std::string> &folderId) const {
  return api_.listSyncEntries(folderId, true);
}

void RemoteScanner::scanFolder(
    const std::string &syncRootId,
    const std::optional<std::string> &remoteFolderId) const {
  const ListSyncEntriesResponse response = fetchEntries(remoteFolderId);
  for (const auto &entry : response.entries) {
    entryRepo_.upsertRemoteEntry({
        .syncRootId = syncRootId,
        .remoteId = entry.remoteId,
        .remoteType = entry.type,
        .remoteFileId = entry.remoteFileId,
        .remoteFolderId = entry.remoteFolderId,
        .remoteParentFolderId = entry.remoteParentFolderId,
        .encryptedName = entry.encryptedName,
        .encryptedMetadata = entry.encryptedMetadata,
        .remoteDeletedAt = entry.deletedAt,
        .remoteUpdatedAt = entry.updatedAt,
    });

    if (entry.type == "folder" && !entry.deletedAt.has_value() &&
        entry.remoteFolderId.has_value()) {
      scanFolder(syncRootId, entry.remoteFolderId);
    }
  }
}
