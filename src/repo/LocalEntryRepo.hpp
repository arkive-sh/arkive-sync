#pragma once

#include "repo/SyncRepoTypes.hpp"

#include <optional>
#include <sqlite3.h>
#include <string>
#include <vector>

class LocalPathProtector;

class LocalEntryRepo {
public:
  LocalEntryRepo(sqlite3 *db, LocalPathProtector &pathProtector);

  std::optional<EntryRecord> getEntryById(const std::string &entryId) const;
  std::vector<EntryRecord>
  getEntriesForSyncRoot(const std::string &syncRootId) const;
  std::vector<EntryRecord> listEntriesPendingUpload(size_t limit) const;
  size_t upsertEntries(const std::vector<EntryRecord> &entries) const;
  size_t
  upsertScannedEntries(const std::vector<EntryUpsertRecord> &entries) const;
  size_t markPathDeleted(const std::string &syncRootId,
                         const std::string &relativePath) const;
  size_t markSubtreeDeleted(const std::string &syncRootId,
                            const std::string &relativeDirPath) const;
  size_t markMissingEntriesDeletedUnderPrefix(
      const std::string &syncRootId, const std::string &relativeDirPath) const;
  size_t
  markMissingEntriesDeletedForCurrentScan(const std::string &syncRootId) const;
  void markEntrySynced(const std::string &entryId);
  void markEntryUploaded(const std::string &entryId,
                         const std::string &remoteId);
  std::string computeLocalPathHash(const std::string &localPath) const;

  std::vector<EntryRecord>
  listRemoteDeletedLocalEntries(const std::string &syncRootId) const;
  size_t markEntryDeletedById(const std::string &entryId) const;

private:
  sqlite3 *db_;
  LocalPathProtector &pathProtector_;
};
