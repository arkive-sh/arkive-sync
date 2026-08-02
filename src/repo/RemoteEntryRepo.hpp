#pragma once

#include "repo/EntryRepo.hpp"

#include <filesystem>
#include <optional>
#include <sqlite3.h>
#include <string>

class RemoteEntryRepo {
public:
  explicit RemoteEntryRepo(sqlite3 *db);

  void upsertRemoteEntry(const RemoteEntryUpsert &entry);
  void markEntryUploaded(const std::string &entryId, const std::string &remoteId,
                         const std::optional<std::string> &remoteParentFolderId);
  void markEntryDownloaded(const std::string &entryId);
  void markEntryDownloaded(const std::string &entryId, int64_t size,
                           std::filesystem::file_time_type mtime,
                           const std::string &contentHash);
  void markFolderCreated(const std::string &entryId,
                         const std::string &remoteFolderId,
                         const std::optional<std::string> &remoteParentFolderId);
  void markRootRemoteDeleted(const std::string &syncRootId);

private:
  sqlite3 *db_;
};
