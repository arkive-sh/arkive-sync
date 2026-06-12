#include "./SyncRepo.hpp"

#include "db/SqliteHelpers.hpp"

#include <stdexcept>

namespace {

SyncRoot readSyncRoot(sqlite3_stmt *stmt) {
  const char *id = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
  const char *localPath =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
  const char *localHash =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
  const char *folderId =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
  const char *createdAt =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5));

  if (id == nullptr || localPath == nullptr || localHash == nullptr ||
      createdAt == nullptr) {
    throw std::runtime_error("sync_roots row contained NULL value");
  }

  return SyncRoot{
      .Id = id,
      .localPath = localPath,
      .localHash = localHash,
      .folderId = folderId != nullptr ? folderId : "",
      .enabled = sqlite3_column_int(stmt, 4),
      .createdAt = createdAt,
  };
}

} // namespace

SyncRepo::SyncRepo(sqlite3 *db) : db_(db) {
  if (db == nullptr) {
    throw std::invalid_argument("Queue Repo needs a valid sqlite3 connection");
  }
}

void SyncRepo::upsertSyncRoot(const SyncRoot &input) {
  static constexpr const char *sql = R"sql(
    INSERT INTO sync_roots (
      id,
      local_path,
      local_path_hash,
      folder_id,
      enabled
    ) VALUES (?, ?, ?, ?, ?)
    ON CONFLICT(id) DO UPDATE SET
      local_path = excluded.local_path,
      local_path_hash = excluded.local_path_hash,
      folder_id = excluded.folder_id,
      enabled = excluded.enabled;
      )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, input.Id);
  bindText(db_, stmt.get(), 2, input.localPath);
  bindText(db_, stmt.get(), 3, input.localHash);
  bindText(db_, stmt.get(), 4, input.folderId);
  throwIfBindFailed(db_, sqlite3_bind_int(stmt.get(), 5, input.enabled));

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }
}

std::optional<SyncRoot> SyncRepo::findSyncRootById(
    const std::string &syncRootId) {
  static constexpr const char *sql = R"sql(
    SELECT
      id,
      local_path,
      local_path_hash,
      folder_id,
      enabled,
      created_at
    FROM sync_roots
    WHERE id = ?;
      )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, syncRootId);

  const int rc = sqlite3_step(stmt.get());
  if (rc == SQLITE_DONE) {
    return std::nullopt;
  }
  if (rc != SQLITE_ROW) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }

  return readSyncRoot(stmt.get());
}
