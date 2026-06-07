#pragma once

#include "sync/SyncMode.hpp"

#include <string>
#include <vector>

enum class ReconcileActionType {
  ApplyRemoteDeleteFile,
  ApplyRemoteDeleteFolder,
  PreserveLocalFileAsHistory,
  PreserveLocalFolderAsHistory,
  IgnoreRemoteDelete,
};

struct ReconcileAction {
  ReconcileActionType type;
  std::string syncRootId;
  std::string entryId;
  std::string localPath;
  std::string reason;
};

struct ReconcilePlan {
  std::vector<ReconcileAction> actions;
};

class LocalEntryRepo;

class ReconcileEngine {
public:
  explicit ReconcileEngine(LocalEntryRepo &localEntries);

  ReconcilePlan plan(const std::string &syncRootId,
                     const SyncModeSpec &mode) const;

private:
  void appendRemoteDeleteActions(ReconcilePlan &plan,
                                 const std::string &syncRootId,
                                 const SyncModeSpec &mode) const;
  LocalEntryRepo &localEntries_;
};
