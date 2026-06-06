#include "sync/RemoteScanner.hpp"

#include "api/ArkiveApi.hpp"
#include "repo/SyncRepo.hpp"

size_t RemoteScanResult::totalEntryCount() const {
  size_t total = 0;
  for (const auto &root : roots) {
    total += root.response.entries.size();
  }
  return total;
}

RemoteScanner::RemoteScanner(SyncRepo &syncRepo, ArkiveApi &api)
    : syncRepo_(syncRepo), api_(api) {}

RemoteScanResult RemoteScanner::scanAllRoots(bool includeDeleted) const {
  RemoteScanResult result;

  for (const auto &syncRoot : syncRepo_.getSyncRoots()) {
    if (!syncRoot.enabled) {
      continue;
    }

    result.roots.push_back(RemoteScanRootResult{
        .syncRootId = syncRoot.id,
        .remoteFolderId = syncRoot.folderId,
        .response = scanFolder(syncRoot.folderId, includeDeleted),
    });
  }

  return result;
}

ListSyncEntriesResponse
RemoteScanner::scanFolder(const std::optional<std::string> &folderId,
                          bool includeDeleted) const {
  return api_.listSyncEntries(ListSyncEntriesRequest{
      .folderId = folderId,
      .includeDeleted = includeDeleted,
  });
}
