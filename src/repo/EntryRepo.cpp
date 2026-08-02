#include "repo/EntryRepo.hpp"

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

EntrySyncState parseEntrySyncState(const char *value) {
  if (value == nullptr) {
    throw std::runtime_error("entries.sync_state was NULL");
  }

  const std::string raw(value);
  if (raw == "unchanged") {
    return EntrySyncState::Unchanged;
  }
  if (raw == "pending_upload") {
    return EntrySyncState::PendingUpload;
  }
  if (raw == "deleted") {
    return EntrySyncState::Deleted;
  }

  throw std::runtime_error("Unknown entries.sync_state: " + raw);
}

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
  const char *id = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
  const char *remoteId =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
  const char *remoteFileId =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
  const char *localDeletedAt =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
  const char *remoteDeletedAt =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
  const char *syncRootId =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5));
  const char *relativePath =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 6));
  const char *parentFolderId =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 7));
  const char *localSize =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 8));
  const unsigned char *localMtime = sqlite3_column_text(stmt, 9);
  const char *contentHash =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 10));
  const char *syncedContentHash =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 11));
  const char *remoteUpdatedAt =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 12));
  const char *syncedRemoteUpdatedAt =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 13));
  const char *conflictState =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 14));
  const char *syncState =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 15));
  const char *lastSeenScanJobId =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 16));
  const int isDirectory = sqlite3_column_int(stmt, 17);

  if (id == nullptr || syncRootId == nullptr || relativePath == nullptr ||
      syncState == nullptr) {
    throw std::runtime_error("entries row contained NULL value");
  }

  const EntrySyncState parsedSyncState = parseEntrySyncState(syncState);

  return Entry{
      .id = id,
      .remoteId = remoteId != nullptr ? std::optional<std::string>(remoteId)
                                      : std::nullopt,
      .remoteFileId = remoteFileId != nullptr
                          ? std::optional<std::string>(remoteFileId)
                          : std::nullopt,
      .localDeletedAt = localDeletedAt != nullptr
                            ? std::optional<std::string>(localDeletedAt)
                            : std::nullopt,
      .remoteDeletedAt = remoteDeletedAt != nullptr
                             ? std::optional<std::string>(remoteDeletedAt)
                             : std::nullopt,
      .syncRootId = syncRootId,
      .relativePath = relativePath,
      .isDirectory = isDirectory != 0,
      .deleted = parsedSyncState == EntrySyncState::Deleted,
      .parentFolderId = parentFolderId != nullptr
                            ? std::optional<std::string>(parentFolderId)
                            : std::nullopt,
      .size = localSize != nullptr
                  ? std::optional<int64_t>(std::stoll(localSize))
                  : std::nullopt,
      .mtime = parseMtime(localMtime),
      .contentHash = contentHash != nullptr
                         ? std::optional<std::string>(contentHash)
                         : std::nullopt,
      .syncedContentHash = syncedContentHash != nullptr
                               ? std::optional<std::string>(syncedContentHash)
                               : std::nullopt,
      .remoteUpdatedAt = remoteUpdatedAt != nullptr
                             ? std::optional<std::string>(remoteUpdatedAt)
                             : std::nullopt,
      .syncedRemoteUpdatedAt = syncedRemoteUpdatedAt != nullptr
                                   ? std::optional<std::string>(
                                         syncedRemoteUpdatedAt)
                                   : std::nullopt,
      .conflictState = conflictState != nullptr
                           ? std::optional<std::string>(conflictState)
                           : std::nullopt,
      .syncState = parsedSyncState,
      .lastSeenScanJobId = lastSeenScanJobId != nullptr
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

std::optional<Entry> EntryRepo::getEntryById(const std::string &entryId) {
  static constexpr const char *sql = R"sql(
SELECT
  id,
  remote_id,
  remote_file_id,
  local_deleted_at,
  remote_deleted_at,
  sync_root_id,
  local_path,
  parent_folder_id,
  local_size,
  local_mtime,
  local_content_hash,
  synced_content_hash,
  remote_updated_at,
  synced_remote_updated_at,
  conflict_state,
  sync_state,
  last_seen_scan_job_id,
  is_directory
FROM entries
WHERE id = ?
LIMIT 1;
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, entryId);

  const int rc = sqlite3_step(stmt.get());
  if (rc == SQLITE_DONE) {
    return std::nullopt;
  }
  if (rc != SQLITE_ROW) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }

  return readEntry(stmt.get());
}

std::optional<Entry>
EntryRepo::findEntryByRemoteId(const std::string &syncRootId,
                               const std::string &remoteId) {
  static constexpr const char *sql = R"sql(
SELECT
  id,
  remote_id,
  remote_file_id,
  local_deleted_at,
  remote_deleted_at,
  sync_root_id,
  local_path,
  parent_folder_id,
  local_size,
  local_mtime,
  local_content_hash,
  synced_content_hash,
  remote_updated_at,
  synced_remote_updated_at,
  conflict_state,
  sync_state,
  last_seen_scan_job_id,
  is_directory
FROM entries
WHERE sync_root_id = ?
  AND remote_id = ?
LIMIT 1;
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, syncRootId);
  bindText(db_, stmt.get(), 2, remoteId);

  const int rc = sqlite3_step(stmt.get());
  if (rc == SQLITE_DONE) {
    return std::nullopt;
  }
  if (rc != SQLITE_ROW) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }

  return readEntry(stmt.get());
}

std::optional<Entry>
EntryRepo::findEntryByPath(const std::string &syncRootId,
                           const std::string &relativePath) {
  static constexpr const char *sql = R"sql(
SELECT
  id,
  remote_id,
  remote_file_id,
  local_deleted_at,
  remote_deleted_at,
  sync_root_id,
  local_path,
  parent_folder_id,
  local_size,
  local_mtime,
  local_content_hash,
  synced_content_hash,
  remote_updated_at,
  synced_remote_updated_at,
  conflict_state,
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
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, syncRootId);
  bindText(db_, stmt.get(), 2, relativePath);

  const int rc = sqlite3_step(stmt.get());
  if (rc == SQLITE_DONE) {
    return std::nullopt;
  }
  if (rc != SQLITE_ROW) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }

  return readEntry(stmt.get());
}

std::vector<Entry> EntryRepo::listEntriesBySyncRootId(
    const std::string &syncRootId) {
  static constexpr const char *sql = R"sql(
SELECT
  id,
  remote_id,
  remote_file_id,
  local_deleted_at,
  remote_deleted_at,
  sync_root_id,
  local_path,
  parent_folder_id,
  local_size,
  local_mtime,
  local_content_hash,
  synced_content_hash,
  remote_updated_at,
  synced_remote_updated_at,
  conflict_state,
  sync_state,
  last_seen_scan_job_id,
  is_directory
FROM entries
WHERE sync_root_id = ?
ORDER BY local_path ASC;
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, syncRootId);

  std::vector<Entry> entries;
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) {
      break;
    }
    if (rc != SQLITE_ROW) {
      throw std::runtime_error(std::string("Step failed: ") +
                               sqlite3_errmsg(db_));
    }

    entries.push_back(readEntry(stmt.get()));
  }

  return entries;
}

std::vector<Entry> EntryRepo::listPendingUploadDirectoriesBySyncRootId(
    const std::string &syncRootId) {
  static constexpr const char *sql = R"sql(
SELECT
  id,
  remote_id,
  remote_file_id,
  local_deleted_at,
  remote_deleted_at,
  sync_root_id,
  local_path,
  parent_folder_id,
  local_size,
  local_mtime,
  local_content_hash,
  synced_content_hash,
  remote_updated_at,
  synced_remote_updated_at,
  conflict_state,
  sync_state,
  last_seen_scan_job_id,
  is_directory
FROM entries
WHERE sync_root_id = ?
  AND sync_state = 'pending_upload'
  AND is_directory = 1
ORDER BY local_path ASC;
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, syncRootId);

  std::vector<Entry> entries;
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) {
      break;
    }
    if (rc != SQLITE_ROW) {
      throw std::runtime_error(std::string("Step failed: ") +
                               sqlite3_errmsg(db_));
    }
    entries.push_back(readEntry(stmt.get()));
  }

  return entries;
}

std::vector<Entry>
EntryRepo::listPendingUploadFilesBySyncRootId(const std::string &syncRootId) {
  static constexpr const char *sql = R"sql(
SELECT
  id,
  remote_id,
  remote_file_id,
  local_deleted_at,
  remote_deleted_at,
  sync_root_id,
  local_path,
  parent_folder_id,
  local_size,
  local_mtime,
  local_content_hash,
  synced_content_hash,
  remote_updated_at,
  synced_remote_updated_at,
  conflict_state,
  sync_state,
  last_seen_scan_job_id,
  is_directory
FROM entries
WHERE sync_root_id = ?
  AND sync_state = 'pending_upload'
  AND is_directory = 0
ORDER BY local_path ASC;
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, syncRootId);

  std::vector<Entry> entries;
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) {
      break;
    }
    if (rc != SQLITE_ROW) {
      throw std::runtime_error(std::string("Step failed: ") +
                               sqlite3_errmsg(db_));
    }
    entries.push_back(readEntry(stmt.get()));
  }

  return entries;
}

void EntryRepo::upsertDirectoryEntry(const DirectoryEntryUpsert &entry) {
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
  sync_state = CASE
    WHEN entries.remote_id IS NULL OR entries.sync_state = 'deleted'
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

void EntryRepo::upsertFileEntry(const FileEntryUpsert &entry) {
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
  local_size = excluded.local_size,
  local_mtime = excluded.local_mtime,
  local_content_hash = excluded.local_content_hash,
  sync_state = excluded.sync_state,
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

void EntryRepo::upsertRemoteEntry(const RemoteEntryUpsert &entry) {
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

void EntryRepo::markEntryUploaded(
    const std::string &entryId, const std::string &remoteId,
    const std::optional<std::string> &remoteParentFolderId) {
  static constexpr const char *sql = R"sql(
UPDATE entries
SET
  remote_id = ?,
  remote_file_id = ?,
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

void EntryRepo::markEntryDownloaded(const std::string &entryId) {
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

void EntryRepo::markEntryDownloaded(const std::string &entryId, int64_t size,
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

void EntryRepo::markFolderCreated(
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

void EntryRepo::markPathDeleted(const std::string &syncRootId,
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

void EntryRepo::markSubtreeEntriesNotSeenInScanDeleted(
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

void EntryRepo::markRootRemoteDeleted(const std::string &syncRootId) {
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
