#pragma once

#include "api/ArkiveApi.hpp"
#include "repo/SyncRepoTypes.hpp"

#include <sqlite3.h>
#include <string>

class LocalPathProtector;

class RemoteEntryRepo {
public:
  RemoteEntryRepo(sqlite3 *db, LocalPathProtector &pathProtector);

  RemoteEntryUpsertAction
  upsertRemoteEntry(const std::string &syncRootId,
                    const SyncEntryResponse &entry) const;

private:
  sqlite3 *db_;
  LocalPathProtector &pathProtector_;
};
