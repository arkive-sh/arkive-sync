#include "sync/Reconcile.hpp"

#include <stdexcept>

Reconcile::Reconcile(SyncMode mode) : mode_(mode) {}

std::vector<ReconcileDecision>
Reconcile::decide(const ListSyncEntriesResponse &remoteEntries) const {
  std::vector<ReconcileDecision> decisions;
  decisions.reserve(remoteEntries.entries.size());

  for (const auto &entry : remoteEntries.entries) {
    decisions.push_back(ReconcileDecision{
        .entryId = entry.id,
        .action = ReconcileAction::Noop,
    });
  }

  return decisions;
}

const SyncModeSpec &Reconcile::spec() const {
  const SyncModeSpec *resolved = findSyncMode(mode_);
  if (resolved == nullptr) {
    throw std::runtime_error("Unknown sync mode");
  }
  return *resolved;
}
