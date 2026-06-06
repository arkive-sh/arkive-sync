#pragma once

#include "api/ArkiveApi.hpp"
#include "sync/SyncMode.hpp"

#include <vector>

enum class ReconcileAction {
  Noop,
};

struct ReconcileDecision {
  std::string entryId;
  ReconcileAction action;
};

class Reconcile {
public:
  explicit Reconcile(SyncMode mode);

  std::vector<ReconcileDecision>
  decide(const ListSyncEntriesResponse &remoteEntries) const;

  SyncMode mode() const { return mode_; }
  const SyncModeSpec &spec() const;

private:
  SyncMode mode_;
};
