#include "sync/RemoteScanner.hpp"

#include "api/ArkiveApi.hpp"
#include "repo/SyncRepo.hpp"
#include <spdlog/spdlog.h>

RemoteScanner::RemoteScanner(SyncRepo &syncRepo, ArkiveApi &api)
    : syncRepo_(syncRepo), api_(api) {}

void RemoteScanner::scanAllRootsAndStore(bool includeDeleted) const {
  for (const auto &syncRoot : syncRepo_.getSyncRoots()) {
    if (!syncRoot.enabled) {
      continue;
    }

    scanFolderAndStore(syncRoot.id, syncRoot.folderId, includeDeleted);
  }
}

void RemoteScanner::scanFolderAndStore(const std::string &syncRootId,
                                       const std::optional<std::string> &folderId,
                                       bool includeDeleted) const {
  std::optional<std::string> cursor;
  std::vector<std::string> childFolderIds;

  while (true) {
    const ListSyncEntriesResponse response =
        api_.listSyncEntries(ListSyncEntriesRequest{
            .folderId = folderId,
            .includeDeleted = includeDeleted,
            .limit = kPageLimit,
            .cursor = cursor,
        });

    for (const auto &entry : response.entries) {
      if (entry.type == "folder" && !entry.deletedAt.has_value()) {
        childFolderIds.push_back(entry.id);
      }

      switch (syncRepo_.upsertRemoteEntry(syncRootId, entry)) {
      case RemoteEntryUpsertAction::Created:
        spdlog::info("remote created root={} remote_id={} type={}", syncRootId,
                     entry.id, entry.type);
        break;
      case RemoteEntryUpsertAction::Updated:
        spdlog::info("remote updated root={} remote_id={} type={}", syncRootId,
                     entry.id, entry.type);
        break;
      case RemoteEntryUpsertAction::Deleted:
        spdlog::info("remote deleted root={} remote_id={} type={}", syncRootId,
                     entry.id, entry.type);
        break;
      case RemoteEntryUpsertAction::Unchanged:
        break;
      }
    }

    if (!response.hasMore || !response.nextCursor.has_value()) {
      break;
    }

    cursor = response.nextCursor;
  }

  for (const auto &childFolderId : childFolderIds) {
    scanFolderAndStore(syncRootId, childFolderId, includeDeleted);
  }
}
