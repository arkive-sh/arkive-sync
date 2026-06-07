#pragma once

#include <array>
#include <optional>
#include <string_view>

enum class SyncModeDirection {
  LocalToRemote,
  RemoteToLocal,
  Bidirectional,
};

enum class SyncMode {
  LocalMirror,
  LocalArchive,
  RemoteMirror,
  RemoteArchive,
  BidirectionalMirror,
  BidirectionalVersioned,
};

enum class DeletePolicy {
  PropagateDelete,
  IgnoreDelete,
  TombstoneOnly,
  PreserveAsHistory,
};

struct SyncModeSpec {
  SyncMode mode;
  std::string_view key;
  std::string_view title;
  std::string_view description;
  SyncModeDirection direction;
  bool localCreates;
  bool localUpdates;
  bool localDeletes;
  bool remoteCreates;
  bool remoteUpdates;
  bool remoteDeletes;
  DeletePolicy localDeletePolicy;
  DeletePolicy remoteDeletePolicy;
  bool preservesHistory;
  bool needsConflictResolution;
};

inline constexpr std::array<SyncModeSpec, 6> kSyncModes{{
    {
        .mode = SyncMode::LocalMirror,
        .key = "local_mirror",
        .title = "Local Mirror",
        .description = "Local side is source of truth. Remote should mirror "
                       "current local state.",
        .direction = SyncModeDirection::LocalToRemote,
        .localCreates = true,
        .localUpdates = true,
        .localDeletes = true,
        .remoteCreates = false,
        .remoteUpdates = false,
        .remoteDeletes = false,
        .localDeletePolicy = DeletePolicy::PropagateDelete,
        .remoteDeletePolicy = DeletePolicy::IgnoreDelete,
        .preservesHistory = false,
        .needsConflictResolution = false,
    },
    {
        .mode = SyncMode::LocalArchive,
        .key = "local_archive",
        .title = "Local Archive",
        .description = "Local side pushes new revisions to remote, while "
                       "remote keeps prior history.",
        .direction = SyncModeDirection::LocalToRemote,
        .localCreates = true,
        .localUpdates = true,
        .localDeletes = false,
        .remoteCreates = false,
        .remoteUpdates = false,
        .remoteDeletes = false,
        .localDeletePolicy = DeletePolicy::PreserveAsHistory,
        .remoteDeletePolicy = DeletePolicy::IgnoreDelete,
        .preservesHistory = true,
        .needsConflictResolution = false,
    },
    {
        .mode = SyncMode::RemoteMirror,
        .key = "remote_mirror",
        .title = "Remote Mirror",
        .description = "Remote side is source of truth. Local should mirror "
                       "current remote state.",
        .direction = SyncModeDirection::RemoteToLocal,
        .localCreates = false,
        .localUpdates = false,
        .localDeletes = false,
        .remoteCreates = true,
        .remoteUpdates = true,
        .remoteDeletes = true,
        .localDeletePolicy = DeletePolicy::IgnoreDelete,
        .remoteDeletePolicy = DeletePolicy::PropagateDelete,
        .preservesHistory = false,
        .needsConflictResolution = false,
    },
    {
        .mode = SyncMode::RemoteArchive,
        .key = "remote_archive",
        .title = "Remote Archive",
        .description = "Remote side pushes revisions to local while keeping "
                       "older remote history intact.",
        .direction = SyncModeDirection::RemoteToLocal,
        .localCreates = false,
        .localUpdates = false,
        .localDeletes = false,
        .remoteCreates = true,
        .remoteUpdates = true,
        .remoteDeletes = false,
        .localDeletePolicy = DeletePolicy::IgnoreDelete,
        .remoteDeletePolicy = DeletePolicy::PreserveAsHistory,
        .preservesHistory = true,
        .needsConflictResolution = false,
    },
    {
        .mode = SyncMode::BidirectionalMirror,
        .key = "bidirectional_mirror",
        .title = "Bidirectional Mirror",
        .description = "Both sides converge on one live state. Conflicting "
                       "edits need explicit resolution.",
        .direction = SyncModeDirection::Bidirectional,
        .localCreates = true,
        .localUpdates = true,
        .localDeletes = true,
        .remoteCreates = true,
        .remoteUpdates = true,
        .remoteDeletes = true,
        .localDeletePolicy = DeletePolicy::PropagateDelete,
        .remoteDeletePolicy = DeletePolicy::PropagateDelete,
        .preservesHistory = false,
        .needsConflictResolution = true,
    },
    {
        .mode = SyncMode::BidirectionalVersioned,
        .key = "bidirectional_versioned",
        .title = "Bidirectional Versioned",
        .description = "Both sides sync changes while preserving conflicting "
                       "or superseded revisions as history.",
        .direction = SyncModeDirection::Bidirectional,
        .localCreates = true,
        .localUpdates = true,
        .localDeletes = true,
        .remoteCreates = true,
        .remoteUpdates = true,
        .remoteDeletes = true,
        .localDeletePolicy = DeletePolicy::PreserveAsHistory,
        .remoteDeletePolicy = DeletePolicy::PreserveAsHistory,
        .preservesHistory = true,
        .needsConflictResolution = true,
    },
}};

inline constexpr const SyncModeSpec &defaultSyncMode() {
  return kSyncModes.front();
}

inline constexpr const SyncModeSpec *findSyncMode(SyncMode mode) {
  for (const auto &spec : kSyncModes) {
    if (spec.mode == mode) {
      return &spec;
    }
  }

  return nullptr;
}

inline constexpr std::optional<SyncMode> parseSyncMode(std::string_view key) {
  for (const auto &spec : kSyncModes) {
    if (spec.key == key) {
      return spec.mode;
    }
  }

  return std::nullopt;
}
