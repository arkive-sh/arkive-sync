#pragma once

#include "repo/LocalEntryRepo.hpp"
#include "repo/SyncRepoTypes.hpp"
#include "sync/Reconcile.hpp"

class ReconcileActionApplier {
public:
  explicit ReconcileActionApplier(LocalEntryRepo &localEntries);

  void apply(const SyncRootRecord &root, const ReconcilePlan &plan) const;

private:
  void applyDeleteLocalFile(const SyncRootRecord &root,
                            const ReconcileAction &action) const;
  void applyDeleteLocalFolder(const SyncRootRecord &root,
                              const ReconcileAction &action) const;

  LocalEntryRepo &localEntries_;
};
