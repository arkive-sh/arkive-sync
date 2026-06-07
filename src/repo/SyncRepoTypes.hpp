#pragma once

#include <cstdint>
#include <optional>
#include <string>

struct SyncRootRecord {
  std::string id;
  std::string localPath;
  std::optional<std::string> folderId;
  bool enabled;
};

struct EntryRecord {
  std::string id;
  std::optional<std::string> remoteId;
  std::optional<std::string> remoteFileId;
  std::optional<std::string> remoteFolderId;
  std::string syncRootId;
  std::string remoteType;
  std::string localPath;
  bool isDirectory;
  std::optional<std::string> parentFolderId;
  std::optional<std::string> remoteParentFolderId;
  std::optional<std::string> encryptedName;
  std::optional<int64_t> localSize;
  std::optional<std::string> localMtime;
  std::optional<std::string> localHash;
  std::optional<std::string> remoteUpdatedAt;
  std::optional<std::string> remoteDeletedAt;
  std::optional<std::string> remotePurgedAt;
  std::optional<std::string> lastRemoteSeenAt;
  std::string syncState;
  std::optional<std::string> lastSyncedAt;
};

struct EntryIdentity {
  std::string id;
  std::optional<std::string> remoteId;
  std::optional<std::string> remoteFileId;
  std::optional<std::string> remoteFolderId;
  bool isDirectory;
  std::optional<std::string> parentFolderId;
  std::optional<std::string> remoteParentFolderId;
  std::optional<std::string> encryptedName;
  std::optional<int64_t> localSize;
  std::optional<std::string> localMtime;
  std::optional<std::string> localHash;
  std::optional<std::string> remoteUpdatedAt;
  std::optional<std::string> remoteDeletedAt;
  std::optional<std::string> remotePurgedAt;
  std::optional<std::string> lastRemoteSeenAt;
  std::string syncState;
  std::optional<std::string> lastSyncedAt;
};

struct EntryUpsertRecord {
  EntryRecord entry;
  std::string localPathHash;
};

enum class RemoteEntryUpsertAction {
  Unchanged,
  Created,
  Updated,
  Deleted,
};
