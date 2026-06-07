#include "sync/ReconcileActionApplier.hpp"

#include <filesystem>
#include <spdlog/spdlog.h>

ReconcileActionApplier::ReconcileActionApplier(LocalEntryRepo &localEntries)
    : localEntries_(localEntries) {}

void ReconcileActionApplier::apply(const SyncRootRecord &root,
                                   const ReconcilePlan &plan) const {
  for (const auto &action : plan.actions) {
    switch (action.type) {
    case ReconcileActionType::ApplyRemoteDeleteFile:
      applyDeleteLocalFile(root, action);
      break;
    case ReconcileActionType::ApplyRemoteDeleteFolder:
      applyDeleteLocalFolder(root, action);
      break;
    case ReconcileActionType::PreserveLocalFileAsHistory:
    case ReconcileActionType::PreserveLocalFolderAsHistory:
    case ReconcileActionType::IgnoreRemoteDelete:
      break;
    }
  }
}

void ReconcileActionApplier::applyDeleteLocalFile(
    const SyncRootRecord &root, const ReconcileAction &action) const {
  const auto fullPath =
      std::filesystem::path(root.localPath) / action.localPath;

  if (std::filesystem::exists(fullPath) &&
      std::filesystem::is_regular_file(fullPath)) {
    std::filesystem::remove(fullPath);
  }

  localEntries_.markEntryDeletedById(action.entryId);
  spdlog::info("reconcile applied delete_local_file root={} entry={} path={}",
               root.id, action.entryId, action.localPath);
}

void ReconcileActionApplier::applyDeleteLocalFolder(
    const SyncRootRecord &root, const ReconcileAction &action) const {
  const auto fullPath =
      std::filesystem::path(root.localPath) / action.localPath;

  if (std::filesystem::exists(fullPath) &&
      std::filesystem::is_directory(fullPath)) {
    std::filesystem::remove_all(fullPath);
  }

  localEntries_.markEntryDeletedById(action.entryId);
  spdlog::info(
      "reconcile applied delete_local_folder root={} entry={} path={}",
      root.id, action.entryId, action.localPath);
}
