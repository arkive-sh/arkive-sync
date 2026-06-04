#pragma once

#include <optional>
#include <sqlite3.h>
#include <string>
#include <vector>

class LocalPathProtector;

struct SyncRootRecord {
  std::string id;
  std::string localPath;
  std::optional<std::string> folderId;
  bool enabled;
};

struct EntryRecord {
  std::string id;
  std::optional<std::string> remoteId;
  std::string syncRootId;
  std::string remoteType;
  std::string localPath;
  bool isDirectory;
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
  SyncRepo(sqlite3 *db, LocalPathProtector &pathProtector);

  std::optional<SyncRootRecord>
  getSyncRootById(const std::string &syncRootId) const;
  std::optional<SyncRootRecord>
  getSyncRootByLocalPath(const std::string &localPath) const;
  std::vector<SyncRootRecord> getSyncRoots() const;
  std::optional<EntryRecord> getEntryById(const std::string &entryId) const;
  std::optional<EntryRecord>
  getEntryByLocalPath(const std::string &syncRootId,
                      const std::string &localPath) const;
  void upsertSyncRoot(const SyncRootRecord &syncRoot) const;
  std::vector<EntryRecord>
  getEntriesForSyncRoot(const std::string &syncRootId) const;
  std::vector<EntryRecord> listPendingUploadEntries(size_t limit) const;
  void upsertEntries(const std::vector<EntryRecord> &entries) const;
  void markEntriesUnseen(const std::string &syncRootId) const;
  void markUnseenEntriesDeleted(const std::string &syncRootId) const;

  void markEntrySynced(const std::string &entryId);
  void markEntryUploaded(const std::string &entryId, const std::string &remoteId);

private:
  sqlite3 *db_;
  LocalPathProtector &pathProtector_;
};
