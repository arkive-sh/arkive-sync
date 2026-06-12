#include "repo/EntryRepo.hpp"

#include "db/SqliteHelpers.hpp"
#include "helpers/GenUUID.hpp"

#include <chrono>
#include <stdexcept>

namespace {

std::string toMtimeString(const std::filesystem::file_time_type &time) {
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      time.time_since_epoch())
                      .count();
  return std::to_string(static_cast<long long>(ms));
}

std::optional<std::filesystem::file_time_type>
parseMtime(const unsigned char *raw) {
  if (raw == nullptr) {
    return std::nullopt;
  }

  const auto ms = std::stoll(reinterpret_cast<const char *>(raw));
  return std::filesystem::file_time_type(std::chrono::milliseconds(ms));
}

Entry readEntry(sqlite3_stmt *stmt) {
  const char *syncRootId =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
  const char *relativePath =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
  const char *localSize =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
  const unsigned char *localMtime = sqlite3_column_text(stmt, 3);
  const char *localHash =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
  const char *syncState =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5));
  const char *lastSeenScanJobId =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 6));
  const int isDirectory = sqlite3_column_int(stmt, 7);

  if (syncRootId == nullptr || relativePath == nullptr || syncState == nullptr) {
    throw std::runtime_error("entries row contained NULL value");
  }

  return Entry{
      .syncRootId = syncRootId,
      .relativePath = relativePath,
      .isDirectory = isDirectory != 0,
      .deleted = std::string(syncState) == "deleted",
      .size = localSize != nullptr ? std::optional<int64_t>(std::stoll(localSize))
                                   : std::nullopt,
      .mtime = parseMtime(localMtime),
      .contentHash = localHash != nullptr ? std::optional<std::string>(localHash)
                                          : std::nullopt,
      .syncState = syncState,
      .lastSeenScanJobId =
          lastSeenScanJobId != nullptr
              ? std::optional<std::string>(lastSeenScanJobId)
              : std::nullopt,
  };
}

} // namespace

EntryRepo::EntryRepo(sqlite3 *db) : db_(db) {
  if (db == nullptr) {
    throw std::invalid_argument("EntryRepo needs valid sqlite3 connection");
  }
}

std::optional<Entry> EntryRepo::findEntryByPath(const std::string &syncRootId,
                                                const std::string &relativePath) {
  static constexpr const char *sql = R"sql(
SELECT
  sync_root_id,
  local_path,
  local_size,
  local_mtime,
  local_hash,
  sync_state,
  last_seen_scan_job_id,
  is_directory
FROM entries
WHERE sync_root_id = ?
  AND local_path = ?
LIMIT 1;
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") + sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, syncRootId);
  bindText(db_, stmt.get(), 2, relativePath);

  const int rc = sqlite3_step(stmt.get());
  if (rc == SQLITE_DONE) {
    return std::nullopt;
  }
  if (rc != SQLITE_ROW) {
    throw std::runtime_error(std::string("Step failed: ") + sqlite3_errmsg(db_));
  }

  return readEntry(stmt.get());
}

void EntryRepo::upsertDirectoryEntry(const DirectoryEntryUpsert &entry) {
  static constexpr const char *sql = R"sql(
INSERT INTO entries (
  id,
  sync_root_id,
  remote_type,
  local_path,
  local_path_hash,
  is_directory,
  sync_state,
  last_seen_scan_job_id,
  updated_at
) VALUES (
  ?,
  ?,
  'directory',
  ?,
  ?,
  1,
  'unchanged',
  ?,
  CURRENT_TIMESTAMP
)
ON CONFLICT(sync_root_id, local_path) DO UPDATE SET
  is_directory = 1,
  sync_state = 'unchanged',
  last_seen_scan_job_id = excluded.last_seen_scan_job_id,
  updated_at = CURRENT_TIMESTAMP;
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") + sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, generateUUID());
  bindText(db_, stmt.get(), 2, entry.syncRootId);
  bindText(db_, stmt.get(), 3, entry.relativePath);
  bindText(db_, stmt.get(), 4, entry.relativePath);
  bindText(db_, stmt.get(), 5, entry.lastSeenScanId);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string("Step failed: ") + sqlite3_errmsg(db_));
  }
}

void EntryRepo::upsertFileEntry(const FileEntryUpsert &entry) {
  static constexpr const char *sql = R"sql(
INSERT INTO entries (
  id,
  sync_root_id,
  remote_type,
  local_path,
  local_path_hash,
  is_directory,
  local_size,
  local_mtime,
  local_hash,
  sync_state,
  last_seen_scan_job_id,
  updated_at
) VALUES (
  ?,
  ?,
  'file',
  ?,
  ?,
  0,
  ?,
  ?,
  ?,
  ?,
  ?,
  CURRENT_TIMESTAMP
)
ON CONFLICT(sync_root_id, local_path) DO UPDATE SET
  is_directory = 0,
  local_size = excluded.local_size,
  local_mtime = excluded.local_mtime,
  local_hash = excluded.local_hash,
  sync_state = excluded.sync_state,
  last_seen_scan_job_id = excluded.last_seen_scan_job_id,
  updated_at = CURRENT_TIMESTAMP;
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") + sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, generateUUID());
  bindText(db_, stmt.get(), 2, entry.syncRootId);
  bindText(db_, stmt.get(), 3, entry.relativePath);
  bindText(db_, stmt.get(), 4, entry.relativePath);
  throwIfBindFailed(db_, sqlite3_bind_int64(stmt.get(), 5, entry.size));
  bindText(db_, stmt.get(), 6, toMtimeString(entry.mtime));
  bindText(db_, stmt.get(), 7, entry.contentHash);
  bindText(db_, stmt.get(), 8, entry.syncState);
  bindText(db_, stmt.get(), 9, entry.lastSeenScanId);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string("Step failed: ") + sqlite3_errmsg(db_));
  }
}

void EntryRepo::markEntriesNotSeenInScanDeleted(const std::string &syncRootId,
                                                const std::string &scanJobId) {
  static constexpr const char *sql = R"sql(
UPDATE entries
SET
  sync_state = 'deleted',
  updated_at = CURRENT_TIMESTAMP
WHERE sync_root_id = ?
  AND (last_seen_scan_job_id IS NULL OR last_seen_scan_job_id != ?)
  AND sync_state != 'deleted';
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") + sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, syncRootId);
  bindText(db_, stmt.get(), 2, scanJobId);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string("Step failed: ") + sqlite3_errmsg(db_));
  }
}
