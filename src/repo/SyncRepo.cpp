#include "./SyncRepo.hpp"

#include "db/SqliteHelpers.hpp"

#include <stdexcept>

namespace {

SyncRoot readSyncRoot(sqlite3_stmt *stmt) {
  const char *id = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
  const char *localPath =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
  const char *folderId =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
  const char *syncMode =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
  const char *createdAt =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5));

  if (id == nullptr || localPath == nullptr || createdAt == nullptr) {
    throw std::runtime_error("sync_roots row contained NULL value");
  }

  const auto parsedMode =
      syncMode == nullptr ? std::optional<SyncMode>(SyncMode::TwoWay)
                          : parseSyncModeDb(syncMode);
  if (!parsedMode.has_value()) {
    throw std::runtime_error("sync_roots.sync_mode contained invalid value");
  }

  return SyncRoot{
      .Id = id,
      .localPath = localPath,
      .folderId = folderId != nullptr ? folderId : "",
      .enabled = sqlite3_column_int(stmt, 3),
      .mode = *parsedMode,
      .createdAt = createdAt,
  };
}

} // namespace

SyncRepo::SyncRepo(sqlite3 *db) : db_(db) {
  if (db == nullptr) {
    throw std::invalid_argument("Queue Repo needs a valid sqlite3 connection");
  }
}

std::vector<SyncRoot> SyncRepo::getSyncRoots() {
  static constexpr const char *sql = R"sql(
    SELECT
      id,
      local_path,
      folder_id,
      enabled,
      sync_mode,
      created_at
    FROM sync_roots
    ORDER BY created_at ASC, local_path ASC;
      )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  std::vector<SyncRoot> roots;

  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) {
      break;
    }
    if (rc != SQLITE_ROW) {
      throw std::runtime_error(std::string("Step failed: ") +
                               sqlite3_errmsg(db_));
    }

    roots.push_back(readSyncRoot(stmt.get()));
  }

  return roots;
}

void SyncRepo::upsertSyncRoot(const SyncRoot &input) {
  static constexpr const char *sql = R"sql(
    INSERT INTO sync_roots (
      id,
      local_path,
      folder_id,
      enabled,
      sync_mode
    ) VALUES (?, ?, ?, ?, ?)
    ON CONFLICT(id) DO UPDATE SET
      local_path = excluded.local_path,
      folder_id = excluded.folder_id,
      enabled = excluded.enabled,
      sync_mode = excluded.sync_mode;
      )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, input.Id);
  bindText(db_, stmt.get(), 2, input.localPath);
  bindText(db_, stmt.get(), 3, input.folderId);
  throwIfBindFailed(db_, sqlite3_bind_int(stmt.get(), 4, input.enabled));
  bindText(db_, stmt.get(), 5, toSyncModeDb(input.mode));

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
      folder_id,
      enabled,
      sync_mode,
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
