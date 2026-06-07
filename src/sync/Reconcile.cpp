#include "sync/Reconcile.hpp"

#include "repo/LocalEntryRepo.hpp"

ReconcileEngine::ReconcileEngine(LocalEntryRepo &localEntries)
    : localEntries_(localEntries) {}

ReconcilePlan ReconcileEngine::plan(const std::string &syncRootId,
                                    const SyncModeSpec &mode) const {
  ReconcilePlan plan;

  if (!mode.remoteDeletes) {
    return plan;
  }

  appendRemoteDeleteActions(plan, syncRootId, mode);

  return plan;
}

// Reconcile Actions
void ReconcileEngine::appendRemoteDeleteActions(
    ReconcilePlan &plan, const std::string &syncRootId,
    const SyncModeSpec &mode) const {

  for (const auto &entry :
       localEntries_.listRemoteDeletedLocalEntries(syncRootId)) {

    switch (mode.remoteDeletePolicy) {

    case DeletePolicy::PropagateDelete:
      plan.actions.push_back(ReconcileAction{
          .type = entry.isDirectory
                      ? ReconcileActionType::ApplyRemoteDeleteFolder
                      : ReconcileActionType::ApplyRemoteDeleteFile,
          .syncRootId = syncRootId,
          .entryId = entry.id,
          .localPath = entry.localPath,
          .reason = "remote tombstone",
      });
      break;

    case DeletePolicy::IgnoreDelete:
      break;

    case DeletePolicy::TombstoneOnly:
      // Later:
      // plan.actions.push_back(...)
      break;

    case DeletePolicy::PreserveAsHistory:
      // Later:
      // plan.actions.push_back(...)
      break;
    }
  }
}
