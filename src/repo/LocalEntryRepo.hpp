#pragma once

#include "repo/EntryRepo.hpp"

#include <sqlite3.h>
#include <string>

class LocalEntryRepo {
public:
  explicit LocalEntryRepo(sqlite3 *db);

  void upsertDirectoryEntry(const DirectoryEntryUpsert &entry);
  void upsertFileEntry(const FileEntryUpsert &entry);
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
