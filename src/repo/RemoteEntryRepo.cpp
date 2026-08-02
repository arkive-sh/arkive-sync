#include "repo/RemoteEntryRepo.hpp"

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

} // namespace

RemoteEntryRepo::RemoteEntryRepo(sqlite3 *db) : db_(db) {
  if (db == nullptr) {
    throw std::invalid_argument("RemoteEntryRepo needs valid sqlite3 connection");
  }
}

void RemoteEntryRepo::upsertRemoteEntry(const RemoteEntryUpsert &entry) {
  static constexpr const char *updateSql = R"sql(
UPDATE entries
SET
  local_path = ?,
  remote_type = ?,
  is_directory = ?,
  encrypted_name = ?,
  encrypted_metadata = ?,
  remote_updated_at = ?,
  remote_file_id = ?,
  remote_folder_id = ?,
  remote_parent_folder_id = ?,
  remote_deleted_at = ?,
  last_remote_seen_at = CURRENT_TIMESTAMP,
  sync_state = ?,
  last_synced_at = CURRENT_TIMESTAMP,
  updated_at = CURRENT_TIMESTAMP
WHERE sync_root_id = ?
  AND remote_id = ?;
  )sql";

  sqlite3_stmt *rawUpdateStmt = nullptr;
  if (sqlite3_prepare_v2(db_, updateSql, -1, &rawUpdateStmt, nullptr) !=
      SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  {
    StmtUniquePtr stmt(rawUpdateStmt);
    bindText(db_, stmt.get(), 1, entry.localPath);
    bindText(db_, stmt.get(), 2, entry.remoteType);
    throwIfBindFailed(db_, sqlite3_bind_int(
                               stmt.get(), 3,
                               entry.remoteType == "folder" ? 1 : 0));
    bindOptionalText(db_, stmt.get(), 4, entry.encryptedName);
    bindOptionalText(db_, stmt.get(), 5, entry.encryptedMetadata);
    bindText(db_, stmt.get(), 6, entry.remoteUpdatedAt);
    bindOptionalText(db_, stmt.get(), 7, entry.remoteFileId);
    bindOptionalText(db_, stmt.get(), 8, entry.remoteFolderId);
    bindOptionalText(db_, stmt.get(), 9, entry.remoteParentFolderId);
    bindOptionalText(db_, stmt.get(), 10, entry.remoteDeletedAt);
    bindText(db_, stmt.get(), 11,
             entry.remoteDeletedAt.has_value() ? "deleted" : "unchanged");
    bindText(db_, stmt.get(), 12, entry.syncRootId);
    bindText(db_, stmt.get(), 13, entry.remoteId);

    const int rc = sqlite3_step(stmt.get());
    if (rc != SQLITE_DONE) {
      throw std::runtime_error(std::string("Step failed: ") +
                               sqlite3_errmsg(db_));
    }
  }

  if (sqlite3_changes(db_) > 0) {
    return;
  }

  static constexpr const char *insertSql = R"sql(
INSERT INTO entries (
  id,
  remote_id,
  sync_root_id,
  remote_type,
  local_path,
  is_directory,
  encrypted_name,
  encrypted_metadata,
  remote_updated_at,
  remote_file_id,
  remote_folder_id,
  remote_parent_folder_id,
  remote_deleted_at,
  last_remote_seen_at,
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
  ?,
  CURRENT_TIMESTAMP,
  ?,
  CURRENT_TIMESTAMP,
  CURRENT_TIMESTAMP
)
ON CONFLICT(sync_root_id, local_path) DO UPDATE SET
  remote_id = excluded.remote_id,
  remote_type = excluded.remote_type,
  is_directory = excluded.is_directory,
  encrypted_name = excluded.encrypted_name,
  encrypted_metadata = excluded.encrypted_metadata,
  remote_updated_at = excluded.remote_updated_at,
  remote_file_id = excluded.remote_file_id,
  remote_folder_id = excluded.remote_folder_id,
  remote_parent_folder_id = excluded.remote_parent_folder_id,
  remote_deleted_at = excluded.remote_deleted_at,
  last_remote_seen_at = CURRENT_TIMESTAMP,
  sync_state = excluded.sync_state,
  last_synced_at = CURRENT_TIMESTAMP,
  updated_at = CURRENT_TIMESTAMP;
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, insertSql, -1, &rawStmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, generateUUID());
  bindText(db_, stmt.get(), 2, entry.remoteId);
  bindText(db_, stmt.get(), 3, entry.syncRootId);
  bindText(db_, stmt.get(), 4, entry.remoteType);
  bindText(db_, stmt.get(), 5, entry.localPath);
  throwIfBindFailed(db_, sqlite3_bind_int(
                             stmt.get(), 6,
                             entry.remoteType == "folder" ? 1 : 0));
  bindOptionalText(db_, stmt.get(), 7, entry.encryptedName);
  bindOptionalText(db_, stmt.get(), 8, entry.encryptedMetadata);
  bindText(db_, stmt.get(), 9, entry.remoteUpdatedAt);
  bindOptionalText(db_, stmt.get(), 10, entry.remoteFileId);
  bindOptionalText(db_, stmt.get(), 11, entry.remoteFolderId);
  bindOptionalText(db_, stmt.get(), 12, entry.remoteParentFolderId);
  bindOptionalText(db_, stmt.get(), 13, entry.remoteDeletedAt);
  bindText(db_, stmt.get(), 14,
           entry.remoteDeletedAt.has_value() ? "deleted" : "unchanged");

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }
}

void RemoteEntryRepo::markEntryUploaded(
    const std::string &entryId, const std::string &remoteId,
    const std::optional<std::string> &remoteParentFolderId) {
  static constexpr const char *sql = R"sql(
UPDATE entries
SET
  remote_id = ?,
  remote_file_id = ?,
  remote_parent_folder_id = ?,
  parent_folder_id = ?,
  synced_content_hash = local_content_hash,
  remote_deleted_at = NULL,
  local_deleted_at = NULL,
  conflict_state = 'none',
  conflict_reason = NULL,
  sync_state = 'unchanged',
  last_synced_at = CURRENT_TIMESTAMP,
  updated_at = CURRENT_TIMESTAMP
WHERE id = ?;
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, remoteId);
  bindText(db_, stmt.get(), 2, remoteId);
  bindOptionalText(db_, stmt.get(), 3, remoteParentFolderId);
  bindOptionalText(db_, stmt.get(), 4, remoteParentFolderId);
  bindText(db_, stmt.get(), 5, entryId);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }
}

void RemoteEntryRepo::markEntryDownloaded(const std::string &entryId) {
  static constexpr const char *sql = R"sql(
UPDATE entries
SET
  synced_remote_updated_at = remote_updated_at,
  sync_state = 'unchanged',
  last_synced_at = CURRENT_TIMESTAMP,
  updated_at = CURRENT_TIMESTAMP
WHERE id = ?;
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, entryId);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }
}

void RemoteEntryRepo::markEntryDownloaded(const std::string &entryId,
                                          int64_t size,
                                          std::filesystem::file_time_type mtime,
                                          const std::string &contentHash) {
  static constexpr const char *sql = R"sql(
UPDATE entries
SET
  local_size = ?,
  local_mtime = ?,
  local_content_hash = ?,
  synced_content_hash = ?,
  synced_remote_updated_at = remote_updated_at,
  sync_state = 'unchanged',
  last_synced_at = CURRENT_TIMESTAMP,
  updated_at = CURRENT_TIMESTAMP
WHERE id = ?;
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  throwIfBindFailed(db_, sqlite3_bind_int64(stmt.get(), 1, size));
  bindText(db_, stmt.get(), 2, toMtimeString(mtime));
  bindText(db_, stmt.get(), 3, contentHash);
  bindText(db_, stmt.get(), 4, contentHash);
  bindText(db_, stmt.get(), 5, entryId);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }
}

void RemoteEntryRepo::markFolderCreated(
    const std::string &entryId, const std::string &remoteFolderId,
    const std::optional<std::string> &remoteParentFolderId) {
  static constexpr const char *sql = R"sql(
UPDATE entries
SET
  remote_id = ?,
  remote_folder_id = ?,
  remote_parent_folder_id = ?,
  parent_folder_id = ?,
  sync_state = 'unchanged',
  last_synced_at = CURRENT_TIMESTAMP,
  updated_at = CURRENT_TIMESTAMP
WHERE id = ?;
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, remoteFolderId);
  bindText(db_, stmt.get(), 2, remoteFolderId);
  bindOptionalText(db_, stmt.get(), 3, remoteParentFolderId);
  bindOptionalText(db_, stmt.get(), 4, remoteParentFolderId);
  bindText(db_, stmt.get(), 5, entryId);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }
}

void RemoteEntryRepo::markRootRemoteDeleted(const std::string &syncRootId) {
  static constexpr const char *sql = R"sql(
UPDATE entries
SET
  sync_state = 'deleted',
  local_deleted_at = COALESCE(local_deleted_at, CURRENT_TIMESTAMP),
  remote_deleted_at = COALESCE(remote_deleted_at, CURRENT_TIMESTAMP),
  synced_remote_updated_at = remote_updated_at,
  conflict_state = 'none',
  conflict_reason = NULL,
  updated_at = CURRENT_TIMESTAMP
WHERE sync_root_id = ?;
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, syncRootId);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }
}
