#pragma once

#include "db/SqliteHelpers.hpp"

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

class SyncScanSession {
public:
  SyncScanSession(sqlite3 *db);

  std::optional<EntryIdentity>
  findEntryIdentityByPathHash(const std::string &syncRootId,
                              const std::string &localPathHash) const;
  void recordSeenPath(const std::string &localPathHash) const;

private:
  sqlite3 *db_;
  StmtUniquePtr lookupStmt_;
  StmtUniquePtr markSeenPathStmt_;
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
  SyncScanSession beginScan() const;
  std::string computeLocalPathHash(const std::string &localPath) const;
  void upsertSyncRoot(const SyncRootRecord &syncRoot) const;
  std::vector<EntryRecord>
  getEntriesForSyncRoot(const std::string &syncRootId) const;
  std::vector<EntryRecord> listEntriesPendingUpload(size_t limit) const;
  size_t upsertEntries(const std::vector<EntryRecord> &entries) const;
  size_t upsertScannedEntries(const std::vector<EntryUpsertRecord> &entries) const;
  size_t markPathDeleted(const std::string &syncRootId,
                         const std::string &relativePath) const;
  size_t markSubtreeDeleted(const std::string &syncRootId,
                            const std::string &relativeDirPath) const;
  size_t markMissingEntriesDeletedUnderPrefix(
      const std::string &syncRootId,
      const std::string &relativeDirPath) const;
  size_t markMissingEntriesDeletedForCurrentScan(
      const std::string &syncRootId) const;
  void releaseMemory() const;

  void markEntrySynced(const std::string &entryId);
  void markEntryUploaded(const std::string &entryId, const std::string &remoteId);

private:
  sqlite3 *db_;
  LocalPathProtector &pathProtector_;
};
