#include "repo/ScanRepo.hpp"

#include "db/SqliteHelpers.hpp"

#include <stdexcept>

namespace {

ScanJob readScanJob(sqlite3_stmt *stmt) {
  const char *id = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
  const char *syncRootId =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
  const char *status =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
  const char *cursorPath =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
  const char *startedAt =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
  const char *updatedAt =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5));
  const char *completedAt =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 6));

  if (id == nullptr || syncRootId == nullptr || status == nullptr ||
      startedAt == nullptr || updatedAt == nullptr) {
    throw std::runtime_error("scan_jobs row contained NULL value");
  }

  return ScanJob{
      .id = id,
      .syncRootId = syncRootId,
      .status = status,
      .cursorPath = cursorPath != nullptr ? std::optional<std::string>(cursorPath)
                                          : std::nullopt,
      .startedAt = startedAt,
      .updatedAt = updatedAt,
      .completedAt =
          completedAt != nullptr ? std::optional<std::string>(completedAt)
                                 : std::nullopt,
  };
}

} // namespace

ScanRepo::ScanRepo(sqlite3 *db) : db_(db) {
  if (db == nullptr) {
    throw std::invalid_argument("ScanRepo needs a valid sqlite3 connection");
  }
}

bool ScanRepo::insertScanJob(const ScanJob &scanJob) {
  static constexpr const char *sql = R"sql(
INSERT OR IGNORE INTO scan_jobs (
  id,
  sync_root_id,
  status,
  cursor_path,
  started_at,
  updated_at,
  completed_at
) VALUES (?, ?, ?, ?, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP, NULL);
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, scanJob.id);
  bindText(db_, stmt.get(), 2, scanJob.syncRootId);
  bindText(db_, stmt.get(), 3, scanJob.status);
  bindOptionalText(db_, stmt.get(), 4, scanJob.cursorPath);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }

  return sqlite3_changes(db_) == 1;
}

std::optional<ScanJob> ScanRepo::getScanJob(const std::string &syncRootId) {
  static constexpr const char *sql = R"sql(
SELECT
  id,
  sync_root_id,
  status,
  cursor_path,
  started_at,
  updated_at,
  completed_at
FROM scan_jobs
WHERE sync_root_id = ?
ORDER BY updated_at DESC
LIMIT 1;
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

  return readScanJob(stmt.get());
}
