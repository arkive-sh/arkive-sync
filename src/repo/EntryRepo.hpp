#pragma once

#include <filesystem>
#include <optional>
#include <sqlite3.h>
#include <string>
#include <vector>

enum class EntrySyncState {
  Unchanged,
  PendingUpload,
  Deleted,
};

struct SyncEntryState {
  bool localExists{false};
  bool remoteExists{false};
  bool localDeleted{false};
  bool remoteDeleted{false};
  bool localDirty{false};
  bool remoteDirty{false};
  bool isDirectory{false};
  bool hasConflict{false};
};

struct Entry {
  std::string id;
  std::optional<std::string> remoteId;
  std::optional<std::string> remoteFileId;
  std::optional<std::string> localDeletedAt;
  std::optional<std::string> remoteDeletedAt;
  std::string syncRootId;
  std::string relativePath;
  bool isDirectory{false};
  bool deleted{false};
  std::optional<std::string> parentFolderId;
  std::optional<int64_t> size;
  std::optional<std::filesystem::file_time_type> mtime;
  std::optional<std::string> contentHash;
  std::optional<std::string> syncedContentHash;
  std::optional<std::string> remoteUpdatedAt;
  std::optional<std::string> syncedRemoteUpdatedAt;
  std::optional<std::string> conflictState;
  EntrySyncState syncState;
  std::optional<std::string> lastSeenScanJobId;
};

struct DirectoryEntryUpsert {
  std::string syncRootId;
  std::string relativePath;
  std::string lastSeenScanId;
};

struct FileEntryUpsert {
  std::string syncRootId;
  std::string relativePath;
  int64_t size{0};
  std::filesystem::file_time_type mtime{};
  std::string contentHash;
  EntrySyncState syncState;
  std::string lastSeenScanId;
};

struct RemoteEntryUpsert {
  std::string syncRootId;
  std::string remoteId;
  std::string localPath;
  std::string remoteType;
  std::optional<std::string> remoteFileId;
  std::optional<std::string> remoteFolderId;
  std::optional<std::string> remoteParentFolderId;
  std::optional<std::string> encryptedName;
  std::optional<std::string> encryptedMetadata;
  std::optional<std::string> remoteDeletedAt;
  std::string remoteUpdatedAt;
};

class EntryRepo {
public:
  explicit EntryRepo(sqlite3 *db);

  std::optional<Entry> getEntryById(const std::string &entryId);
  std::optional<Entry> findEntryByRemoteId(const std::string &syncRootId,
                                           const std::string &remoteId);
  std::optional<Entry> findEntryByPath(const std::string &syncRootId,
                                       const std::string &relativePath);
  std::vector<Entry> listEntriesBySyncRootId(const std::string &syncRootId);
  std::vector<Entry> listEntriesBySyncRootIdPage(const std::string &syncRootId,
                                                 int limit, int offset);
  std::vector<Entry> listEntriesBySyncRootIdAfterPath(
      const std::string &syncRootId, const std::optional<std::string> &afterPath,
      int limit);
  std::vector<Entry>
  listPendingUploadDirectoriesBySyncRootId(const std::string &syncRootId);
  std::vector<Entry>
  listPendingUploadFilesBySyncRootId(const std::string &syncRootId);
private:
  sqlite3 *db_;
};
