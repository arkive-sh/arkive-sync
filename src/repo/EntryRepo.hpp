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

struct Entry {
  std::string id;
  std::optional<std::string> remoteId;
  std::string syncRootId;
  std::string relativePath;
  bool isDirectory{false};
  bool deleted{false};
  std::optional<std::string> parentFolderId;
  std::optional<int64_t> size;
  std::optional<std::filesystem::file_time_type> mtime;
  std::optional<std::string> contentHash;
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

class EntryRepo {
public:
  explicit EntryRepo(sqlite3 *db);

  std::optional<Entry> getEntryById(const std::string &entryId);
  std::optional<Entry> findEntryByPath(const std::string &syncRootId,
                                       const std::string &relativePath);
  std::vector<Entry>
  listPendingUploadFilesBySyncRootId(const std::string &syncRootId);
  void upsertDirectoryEntry(const DirectoryEntryUpsert &entry);
  void upsertFileEntry(const FileEntryUpsert &entry);
  void markEntryUploaded(const std::string &entryId, const std::string &remoteId);
  void markEntriesNotSeenInScanDeleted(const std::string &syncRootId,
                                       const std::string &scanJobId);
  void markPathDeleted(const std::string &syncRootId,
                       const std::string &relativePath);
  void markSubtreeEntriesNotSeenInScanDeleted(const std::string &syncRootId,
                                              const std::string &relativePath,
                                              const std::string &scanJobId);

private:
  sqlite3 *db_;
};
