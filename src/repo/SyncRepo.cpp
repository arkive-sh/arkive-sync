#include "repo/SyncRepo.hpp"

#include "db/SqliteHelpers.hpp"
#include "helpers/LocalPathProtector.hpp"

#include <stdexcept>

SyncRepo::SyncRepo(sqlite3 *db, LocalPathProtector &pathProtector)
    : db_(db), roots_(db, pathProtector),
      local_(db, pathProtector), remote_(db, pathProtector) {
  if (db == nullptr) {
    throw std::invalid_argument("Sync Repo needs a valid sqlite3 connection");
  }
}

SyncScanSession SyncRepo::beginScan() const { return SyncScanSession(db_); }

void SyncRepo::releaseMemory() const { ::releaseMemory(db_); }
