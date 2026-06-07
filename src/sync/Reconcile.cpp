#include "sync/Reconcile.hpp"

#include "repo/LocalEntryRepo.hpp"

ReconcileEngine::ReconcileEngine(LocalEntryRepo &localEntries)
    : localEntries_(localEntries) {}

ReconcilePlan
ReconcileEngine::planRemoteDeletes(const std::string &syncRootId,
                                   const SyncModeSpec &mode) const {
  ReconcilePlan plan;

  if (!mode.remoteDeletes) {
    return plan;
  }

  for (const auto &entry :
       localEntries_.listRemoteDeletedLocalEntries(syncRootId)) {
    plan.actions.push_back(ReconcileAction{
        .type = entry.isDirectory ? ReconcileActionType::DeleteLocalFolder
                                  : ReconcileActionType::DeleteLocalFile,
        .syncRootId = syncRootId,
        .entryId = entry.id,
        .localPath = entry.localPath,
        .reason = "remote tombstone",
    });
  }

  return plan;
}
