#pragma once

#include "repo/SyncRepoTypes.hpp"

#include <optional>
#include <sqlite3.h>
#include <string>
#include <vector>

class LocalPathProtector;

class SyncRootRepo {
public:
  SyncRootRepo(sqlite3 *db, LocalPathProtector &pathProtector);

  std::optional<SyncRootRecord>
  getSyncRootById(const std::string &syncRootId) const;
  std::optional<SyncRootRecord>
  getSyncRootByLocalPath(const std::string &localPath) const;
  std::vector<SyncRootRecord> getSyncRoots() const;
  void upsertSyncRoot(const SyncRootRecord &syncRoot) const;

private:
  sqlite3 *db_;
  LocalPathProtector &pathProtector_;
};
