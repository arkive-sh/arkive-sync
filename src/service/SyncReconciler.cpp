#include "service/SyncReconciler.hpp"

#include "repo/EntryRepo.hpp"
#include "repo/SyncRepo.hpp"
#include "sync/SyncPolicy.hpp"
#include "sync/SyncStateClassifier.hpp"

#include <spdlog/spdlog.h>

SyncReconciler::SyncReconciler(EntryRepo &entryRepo) : entryRepo_(entryRepo) {}

void SyncReconciler::reconcileRoot(const SyncRoot &root) {
  for (const auto &entry : entryRepo_.listEntriesBySyncRootId(root.Id)) {
    const SyncEntryState state = SyncStateClassifier::classify(entry);
    const SyncDecision decision = SyncPolicy::decide(state, root.mode);

    spdlog::info(
        "sync reconcile root={} path={} mode={} decision={} local_exists={} "
        "remote_exists={} local_deleted={} remote_deleted={} local_dirty={} "
        "remote_dirty={} conflict={}",
        root.Id, entry.relativePath, toSyncModeDb(root.mode),
        toSyncDecisionName(decision), state.localExists, state.remoteExists,
        state.localDeleted, state.remoteDeleted, state.localDirty,
        state.remoteDirty, state.hasConflict);
  }
}
