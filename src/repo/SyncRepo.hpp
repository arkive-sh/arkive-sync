#pragma once

#include <optional>
#include <sqlite3.h>
#include <string>
#include <vector>

struct SyncRootRecord {
  std::string id;
  std::string localPath;
  std::optional<std::string> folderId;
  bool enabled;
};

struct EntryRecord {
  std::string id;
  std::optional<std::string> remoteId;
  std::string remoteType;
  std::string localPath;
  std::optional<std::string> parentFolderId;
  std::optional<std::string> encryptedName;
  std::optional<int64_t> localSize;
  std::optional<std::string> localMtime;
  std::optional<std::string> localHash;
  std::optional<std::string> remoteUpdatedAt;
  std::string syncState;
  std::optional<std::string> lastSyncedAt;
};

class SyncRepo {
public:
  explicit SyncRepo(sqlite3 *db);

  void upsertSyncRoot(const SyncRootRecord &syncRoot) const;
  void upsertEntries(const std::vector<EntryRecord> &entries) const;

private:
  sqlite3 *db_;
};
