#include "repo/LocalEntryRepo.hpp"

#include "db/SqliteHelpers.hpp"
#include "helpers/GenUUID.hpp"

#include <chrono>
#include <stdexcept>

namespace {

const char *toEntrySyncStateString(EntrySyncState state) {
  switch (state) {
  case EntrySyncState::Unchanged:
    return "unchanged";
  case EntrySyncState::PendingUpload:
    return "pending_upload";
  case EntrySyncState::Deleted:
    return "deleted";
  }

  throw std::runtime_error("Unknown EntrySyncState");
}

std::string toMtimeString(const std::filesystem::file_time_type &time) {
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      time.time_since_epoch())
                      .count();
  return std::to_string(static_cast<long long>(ms));
}

} // namespace

LocalEntryRepo::LocalEntryRepo(sqlite3 *db) : db_(db) {
  if (db == nullptr) {
    throw std::invalid_argument("LocalEntryRepo needs valid sqlite3 connection");
  }
}

void LocalEntryRepo::upsertDirectoryEntry(const DirectoryEntryUpsert &entry) {
  static constexpr const char *sql = R"sql(
INSERT INTO entries (
  id,
  sync_root_id,
  remote_type,
  local_path,
  is_directory,
  sync_state,
  last_seen_scan_job_id,
  updated_at
) VALUES (
  ?,
  ?,
  'directory',
  ?,
  1,
  'pending_upload',
  ?,
  CURRENT_TIMESTAMP
)
ON CONFLICT(sync_root_id, local_path) DO UPDATE SET
  is_directory = 1,
  remote_id = CASE
    WHEN entries.local_deleted_at IS NOT NULL THEN NULL
    ELSE entries.remote_id
  END,
  parent_folder_id = CASE
    WHEN entries.local_deleted_at IS NOT NULL THEN NULL
    ELSE entries.parent_folder_id
  END,
  remote_folder_id = CASE
    WHEN entries.local_deleted_at IS NOT NULL THEN NULL
    ELSE entries.remote_folder_id
  END,
  remote_parent_folder_id = CASE
    WHEN entries.local_deleted_at IS NOT NULL THEN NULL
    ELSE entries.remote_parent_folder_id
  END,
  remote_deleted_at = CASE
    WHEN entries.local_deleted_at IS NOT NULL THEN NULL
    ELSE entries.remote_deleted_at
  END,
  remote_updated_at = CASE
    WHEN entries.local_deleted_at IS NOT NULL THEN NULL
    ELSE entries.remote_updated_at
  END,
  synced_remote_updated_at = CASE
    WHEN entries.local_deleted_at IS NOT NULL THEN NULL
    ELSE entries.synced_remote_updated_at
  END,
  local_deleted_at = NULL,
  conflict_state = CASE
    WHEN entries.local_deleted_at IS NOT NULL THEN 'none'
    ELSE entries.conflict_state
  END,
  conflict_reason = CASE
    WHEN entries.local_deleted_at IS NOT NULL THEN NULL
    ELSE entries.conflict_reason
  END,
  sync_state = CASE
    WHEN entries.local_deleted_at IS NOT NULL
      OR entries.remote_id IS NULL
      OR entries.sync_state = 'deleted'
      THEN 'pending_upload'
    ELSE 'unchanged'
  END,
  last_seen_scan_job_id = excluded.last_seen_scan_job_id,
  updated_at = CURRENT_TIMESTAMP;
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, generateUUID());
  bindText(db_, stmt.get(), 2, entry.syncRootId);
  bindText(db_, stmt.get(), 3, entry.relativePath);
  bindText(db_, stmt.get(), 4, entry.lastSeenScanId);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }
}

void LocalEntryRepo::upsertFileEntry(const FileEntryUpsert &entry) {
  static constexpr const char *sql = R"sql(
INSERT INTO entries (
  id,
  sync_root_id,
  remote_type,
  local_path,
  is_directory,
  local_size,
  local_mtime,
  local_content_hash,
  sync_state,
  last_seen_scan_job_id,
  updated_at
) VALUES (
  ?,
  ?,
  'file',
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
  remote_id = CASE
    WHEN entries.local_deleted_at IS NOT NULL THEN NULL
    ELSE entries.remote_id
  END,
  remote_file_id = CASE
    WHEN entries.local_deleted_at IS NOT NULL THEN NULL
    ELSE entries.remote_file_id
  END,
  parent_folder_id = CASE
    WHEN entries.local_deleted_at IS NOT NULL THEN NULL
    ELSE entries.parent_folder_id
  END,
  remote_folder_id = CASE
    WHEN entries.local_deleted_at IS NOT NULL THEN NULL
    ELSE entries.remote_folder_id
  END,
  remote_parent_folder_id = CASE
    WHEN entries.local_deleted_at IS NOT NULL THEN NULL
    ELSE entries.remote_parent_folder_id
  END,
  remote_deleted_at = CASE
    WHEN entries.local_deleted_at IS NOT NULL THEN NULL
    ELSE entries.remote_deleted_at
  END,
  remote_updated_at = CASE
    WHEN entries.local_deleted_at IS NOT NULL THEN NULL
    ELSE entries.remote_updated_at
  END,
  synced_remote_updated_at = CASE
    WHEN entries.local_deleted_at IS NOT NULL THEN NULL
    ELSE entries.synced_remote_updated_at
  END,
  synced_content_hash = CASE
    WHEN entries.local_deleted_at IS NOT NULL THEN NULL
    ELSE entries.synced_content_hash
  END,
  local_deleted_at = NULL,
  local_size = excluded.local_size,
  local_mtime = excluded.local_mtime,
  local_content_hash = excluded.local_content_hash,
  conflict_state = CASE
    WHEN entries.local_deleted_at IS NOT NULL THEN 'none'
    ELSE entries.conflict_state
  END,
  conflict_reason = CASE
    WHEN entries.local_deleted_at IS NOT NULL THEN NULL
    ELSE entries.conflict_reason
  END,
  sync_state = CASE
    WHEN entries.local_deleted_at IS NOT NULL THEN 'pending_upload'
    ELSE excluded.sync_state
  END,
  last_seen_scan_job_id = excluded.last_seen_scan_job_id,
  updated_at = CURRENT_TIMESTAMP;
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, generateUUID());
  bindText(db_, stmt.get(), 2, entry.syncRootId);
  bindText(db_, stmt.get(), 3, entry.relativePath);
  throwIfBindFailed(db_, sqlite3_bind_int64(stmt.get(), 4, entry.size));
  bindText(db_, stmt.get(), 5, toMtimeString(entry.mtime));
  bindText(db_, stmt.get(), 6, entry.contentHash);
  bindText(db_, stmt.get(), 7, toEntrySyncStateString(entry.syncState));
  bindText(db_, stmt.get(), 8, entry.lastSeenScanId);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }
}

void LocalEntryRepo::markEntriesNotSeenInScanDeleted(
    const std::string &syncRootId, const std::string &scanJobId) {
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
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, syncRootId);
  bindText(db_, stmt.get(), 2, scanJobId);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }
}

void LocalEntryRepo::markPathDeleted(const std::string &syncRootId,
                                     const std::string &relativePath) {
  static constexpr const char *sql = R"sql(
UPDATE entries
SET
  sync_state = 'deleted',
  updated_at = CURRENT_TIMESTAMP
WHERE sync_root_id = ?
  AND sync_state != 'deleted'
  AND (
    local_path = ?
    OR local_path LIKE ?
  );
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, syncRootId);
  bindText(db_, stmt.get(), 2, relativePath);
  bindText(db_, stmt.get(), 3, relativePath + "/%");

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }
}

void LocalEntryRepo::markSubtreeEntriesNotSeenInScanDeleted(
    const std::string &syncRootId, const std::string &relativePath,
    const std::string &scanJobId) {
  static constexpr const char *sql = R"sql(
UPDATE entries
SET
  sync_state = 'deleted',
  updated_at = CURRENT_TIMESTAMP
WHERE sync_root_id = ?
  AND sync_state != 'deleted'
  AND (last_seen_scan_job_id IS NULL OR last_seen_scan_job_id != ?)
  AND (
    local_path = ?
    OR local_path LIKE ?
  );
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, syncRootId);
  bindText(db_, stmt.get(), 2, scanJobId);
  bindText(db_, stmt.get(), 3, relativePath);
  bindText(db_, stmt.get(), 4, relativePath + "/%");

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }
}
