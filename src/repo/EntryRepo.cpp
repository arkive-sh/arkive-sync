#include "repo/EntryRepo.hpp"

#include "db/SqliteHelpers.hpp"

#include <chrono>
#include <stdexcept>

namespace {

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

std::vector<Entry> EntryRepo::listEntriesBySyncRootIdPage(
    const std::string &syncRootId, int limit, int offset) {
  if (limit <= 0 || offset < 0) {
    throw std::invalid_argument("entry page has invalid bounds");
  }

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
ORDER BY local_path ASC
LIMIT ? OFFSET ?;
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, syncRootId);
  throwIfBindFailed(db_, sqlite3_bind_int(stmt.get(), 2, limit));
  throwIfBindFailed(db_, sqlite3_bind_int(stmt.get(), 3, offset));

  std::vector<Entry> entries;
  entries.reserve(static_cast<size_t>(limit));
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

std::vector<Entry> EntryRepo::listEntriesBySyncRootIdAfterPath(
    const std::string &syncRootId, const std::optional<std::string> &afterPath,
    int limit) {
  if (limit <= 0) {
    throw std::invalid_argument("entry page has invalid limit");
  }

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
  AND (? IS NULL OR local_path > ?)
ORDER BY local_path ASC
LIMIT ?;
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, syncRootId);
  bindOptionalText(db_, stmt.get(), 2, afterPath);
  bindOptionalText(db_, stmt.get(), 3, afterPath);
  throwIfBindFailed(db_, sqlite3_bind_int(stmt.get(), 4, limit));

  std::vector<Entry> entries;
  entries.reserve(static_cast<size_t>(limit));
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
