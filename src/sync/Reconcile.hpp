#pragma once

#include "sync/SyncMode.hpp"

#include <string>
#include <vector>

enum class ReconcileActionType {
  DeleteLocalFile,
  DeleteLocalFolder,
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

  ReconcilePlan planRemoteDeletes(const std::string &syncRootId,
                                  const SyncModeSpec &mode) const;

private:
  LocalEntryRepo &localEntries_;
};
