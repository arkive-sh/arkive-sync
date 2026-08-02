#pragma once

#include "repo/EntryRepo.hpp"

class SyncStateClassifier {
public:
  static SyncEntryState classify(const Entry &entry) {
    const bool localDeleted = entry.localDeletedAt.has_value();
    const bool remoteDeleted = entry.remoteDeletedAt.has_value();

    return SyncEntryState{
        .localExists = !localDeleted,
        .remoteExists = entry.remoteId.has_value() && !remoteDeleted,
        .localDeleted = localDeleted,
        .remoteDeleted = remoteDeleted,
        .localDirty = !localDeleted &&
                      entry.contentHash != entry.syncedContentHash,
        .remoteDirty = !remoteDeleted &&
                       entry.remoteUpdatedAt != entry.syncedRemoteUpdatedAt,
        .isDirectory = entry.isDirectory,
        .hasConflict = entry.conflictState.has_value() &&
                       *entry.conflictState != "none",
    };
  }
};
