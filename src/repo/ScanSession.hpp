#pragma once

#include "db/SqliteHelpers.hpp"
#include "repo/SyncRepoTypes.hpp"

#include <optional>
#include <sqlite3.h>
#include <string>

class SyncScanSession {
public:
  explicit SyncScanSession(sqlite3 *db);

  std::optional<EntryIdentity>
  findEntryIdentityByPathHash(const std::string &syncRootId,
                              const std::string &localPathHash) const;
  void recordSeenPath(const std::string &localPathHash) const;

private:
  sqlite3 *db_;
  StmtUniquePtr lookupStmt_;
  StmtUniquePtr markSeenPathStmt_;
};
