#include "sync/RemoteScanner.hpp"
#include "sync/Reconcile.hpp"

#include "api/ArkiveApi.hpp"
#include "repo/SyncRepo.hpp"
#include <spdlog/spdlog.h>

RemoteScanner::RemoteScanner(SyncRepo &syncRepo, ArkiveApi &api)
    : syncRepo_(syncRepo), api_(api) {}

void RemoteScanner::scanAllRootsAndStore(bool includeDeleted) const {
  ReconcileEngine reconcile(syncRepo_.local());

  const SyncModeSpec *mode = findSyncMode(SyncMode::RemoteMirror);
  if (mode == nullptr) {
    throw std::runtime_error("RemoteMirror sync mode missing");
  }

  for (const auto &syncRoot : syncRepo_.roots().getSyncRoots()) {
    if (!syncRoot.enabled) {
      continue;
    }

    scanFolderAndStore(syncRoot.id, syncRoot.folderId, includeDeleted);

    const ReconcilePlan plan = reconcile.planRemoteDeletes(syncRoot.id, *mode);

    for (const auto &action : plan.actions) {
      spdlog::info(
          "reconcile planned action={} root={} entry={} path={} reason={}",
          action.type == ReconcileActionType::DeleteLocalFolder
              ? "delete_local_folder"
              : "delete_local_file",
          action.syncRootId, action.entryId, action.localPath, action.reason);
    }
  }
}

void RemoteScanner::scanFolderAndStore(
    const std::string &syncRootId, const std::optional<std::string> &folderId,
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

      switch (syncRepo_.remote().upsertRemoteEntry(syncRootId, entry)) {
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
