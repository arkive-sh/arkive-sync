#include "repo/SyncRepo.hpp"
#include "db/SqliteHelpers.hpp"

#include <sqlite3.h>
#include <stdexcept>

SyncRepo::SyncRepo(sqlite3 *db) : db_(db) {
  if (db == nullptr) {
    throw std::invalid_argument("Sync Repo needs a valid sqlite3 connection");
  }
}

void SyncRepo::upsertSyncRoot(const SyncRootRecord &syncRoot) const {
  static constexpr const char *upsertSyncRootSql = R"sql(
INSERT INTO sync_roots (
  id,
  local_path,
  folder_id,
  enabled,
  created_at
) VALUES (
  ?,
  ?,
  ?,
  ?,
  CURRENT_TIMESTAMP
)
ON CONFLICT(local_path) DO UPDATE SET
  folder_id = excluded.folder_id,
  enabled = excluded.enabled;
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, upsertSyncRootSql, -1, &rawStmt, nullptr) !=
      SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, syncRoot.id);
  bindText(db_, stmt.get(), 2, syncRoot.localPath);
  bindOptionalText(db_, stmt.get(), 3, syncRoot.folderId);
  throwIfBindFailed(db_, sqlite3_bind_int(stmt.get(), 4, syncRoot.enabled ? 1 : 0));

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }
}

void SyncRepo::upsertEntries(const std::vector<EntryRecord> &entries) const {
  static constexpr const char *upsertEntrySql = R"sql(
INSERT INTO entries (
  id,
  remote_id,
  remote_type,
  local_path,
  parent_folder_id,
  encrypted_name,
  local_size,
  local_mtime,
  local_hash,
  remote_updated_at,
  sync_state,
  last_synced_at,
  updated_at
) VALUES (
  ?,
  ?,
  ?,
  ?,
  ?,
  ?,
  ?,
  ?,
  ?,
  ?,
  ?,
  ?,
  CURRENT_TIMESTAMP
)
ON CONFLICT(local_path) DO UPDATE SET
  remote_type = excluded.remote_type,
  local_size = excluded.local_size,
  local_mtime = excluded.local_mtime,
  sync_state = excluded.sync_state,
  updated_at = CURRENT_TIMESTAMP;
  )sql";

  execOrThrow(db_, "BEGIN TRANSACTION;");

  try {
    sqlite3_stmt *rawStmt = nullptr;
    if (sqlite3_prepare_v2(db_, upsertEntrySql, -1, &rawStmt, nullptr) !=
        SQLITE_OK) {
      throw std::runtime_error(std::string("Prepare failed: ") +
                               sqlite3_errmsg(db_));
    }

    StmtUniquePtr stmt(rawStmt);
    for (const auto &entry : entries) {
      sqlite3_reset(stmt.get());
      sqlite3_clear_bindings(stmt.get());

      bindText(db_, stmt.get(), 1, entry.id);
      bindOptionalText(db_, stmt.get(), 2, entry.remoteId);
      bindText(db_, stmt.get(), 3, entry.remoteType);
      bindText(db_, stmt.get(), 4, entry.localPath);
      bindOptionalText(db_, stmt.get(), 5, entry.parentFolderId);
      bindOptionalText(db_, stmt.get(), 6, entry.encryptedName);
      bindOptionalInt64(db_, stmt.get(), 7, entry.localSize);
      bindOptionalText(db_, stmt.get(), 8, entry.localMtime);
      bindOptionalText(db_, stmt.get(), 9, entry.localHash);
      bindOptionalText(db_, stmt.get(), 10, entry.remoteUpdatedAt);
      bindText(db_, stmt.get(), 11, entry.syncState);
      bindOptionalText(db_, stmt.get(), 12, entry.lastSyncedAt);

      const int rc = sqlite3_step(stmt.get());
      if (rc != SQLITE_DONE) {
        throw std::runtime_error(std::string("Step failed: ") +
                                 sqlite3_errmsg(db_));
      }
    }

    execOrThrow(db_, "COMMIT;");
  } catch (...) {
    sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
    throw;
  }
}
