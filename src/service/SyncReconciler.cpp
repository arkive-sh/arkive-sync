#include "service/SyncReconciler.hpp"

#include "fs/helpers/PathHelpers.hpp"
#include "repo/EntryRepo.hpp"
#include "repo/SyncRepo.hpp"
#include "sync/SyncPolicy.hpp"
#include "sync/SyncStateClassifier.hpp"

#include <filesystem>
#include <spdlog/spdlog.h>

SyncReconciler::SyncReconciler(EntryRepo &entryRepo) : entryRepo_(entryRepo) {}

void SyncReconciler::applyDeleteLocal(const SyncRoot &root,
                                      const std::filesystem::path &path,
                                      bool isDirectory) {
  std::error_code error;
  if (isDirectory) {
    std::filesystem::remove_all(path, error);
  } else {
    std::filesystem::remove(path, error);
  }

  if (error) {
    spdlog::error("Failed to apply delete_local for root {} path {}: {}",
                  root.Id, path.string(), error.message());
  }
}

void SyncReconciler::reconcileRoot(const SyncRoot &root) {
  for (const auto &entry : entryRepo_.listEntriesBySyncRootId(root.Id)) {
    const SyncEntryState state = SyncStateClassifier::classify(entry);
    const SyncDecision decision = SyncPolicy::decide(state, root.mode);
    const std::filesystem::path absolutePath =
        std::filesystem::path(normalizeFsPath(root.localPath)) /
        entry.relativePath;

    if (decision == SyncDecision::DeleteLocal) {
      applyDeleteLocal(root, absolutePath, entry.isDirectory);
    }

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
