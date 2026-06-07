#include "repo/LocalEntryRepo.hpp"

#include "db/SqliteHelpers.hpp"
#include "helpers/LocalPathProtector.hpp"
#include "repo/SyncRepoTypes.hpp"

#include <optional>
#include <sqlite3.h>
#include <stdexcept>
#include <vector>

namespace {

EntryRecord readEntryRecord(sqlite3_stmt *stmt) {
  const char *id = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
  const char *remoteId =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
  const char *remoteFileId =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
  const char *remoteFolderId =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
  const char *syncRootId =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
  const char *remoteType =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5));
  const char *localPath =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 6));
  const char *parentFolderId =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 10));
  const char *remoteParentFolderId =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 11));
  const char *encryptedName =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 12));
  const char *localMtime =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 14));
  const char *localHash =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 15));
  const char *remoteUpdatedAt =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 16));
  const char *remoteDeletedAt =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 17));
  const char *remotePurgedAt =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 18));
  const char *lastRemoteSeenAt =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 19));
  const char *syncState =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 20));
  const char *lastSyncedAt =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 21));

  if (id == nullptr || syncRootId == nullptr || remoteType == nullptr ||
      localPath == nullptr || syncState == nullptr) {
    throw std::invalid_argument("entries row contained NULL value");
  }

  return EntryRecord{
      .id = id,
      .remoteId = remoteId != nullptr ? std::optional<std::string>(remoteId)
                                      : std::nullopt,
      .remoteFileId = remoteFileId != nullptr
                          ? std::optional<std::string>(remoteFileId)
                          : std::nullopt,
      .remoteFolderId = remoteFolderId != nullptr
                            ? std::optional<std::string>(remoteFolderId)
                            : std::nullopt,
      .syncRootId = syncRootId,
      .remoteType = remoteType,
      .localPath = localPath,
      .isDirectory = sqlite3_column_int(stmt, 9) != 0,
      .parentFolderId = parentFolderId != nullptr
                            ? std::optional<std::string>(parentFolderId)
                            : std::nullopt,
      .remoteParentFolderId =
          remoteParentFolderId != nullptr
              ? std::optional<std::string>(remoteParentFolderId)
              : std::nullopt,
      .encryptedName = encryptedName != nullptr
                           ? std::optional<std::string>(encryptedName)
                           : std::nullopt,
      .localSize = sqlite3_column_type(stmt, 13) != SQLITE_NULL
                       ? std::optional<int64_t>(sqlite3_column_int64(stmt, 13))
                       : std::nullopt,
      .localMtime = localMtime != nullptr
                        ? std::optional<std::string>(localMtime)
                        : std::nullopt,
      .localHash = localHash != nullptr ? std::optional<std::string>(localHash)
                                        : std::nullopt,
      .remoteUpdatedAt = remoteUpdatedAt != nullptr
                             ? std::optional<std::string>(remoteUpdatedAt)
                             : std::nullopt,
      .remoteDeletedAt = remoteDeletedAt != nullptr
                             ? std::optional<std::string>(remoteDeletedAt)
                             : std::nullopt,
      .remotePurgedAt = remotePurgedAt != nullptr
                            ? std::optional<std::string>(remotePurgedAt)
                            : std::nullopt,
      .lastRemoteSeenAt = lastRemoteSeenAt != nullptr
                              ? std::optional<std::string>(lastRemoteSeenAt)
                              : std::nullopt,
      .syncState = syncState,
      .lastSyncedAt = lastSyncedAt != nullptr
                          ? std::optional<std::string>(lastSyncedAt)
                          : std::nullopt,
  };
}

bool isPathHashSeen(sqlite3 *db, const std::string &pathHash) {
  static constexpr const char *isPathHashSeenSql = R"sql(
SELECT 1
FROM scan_seen_paths
WHERE local_path_hash = ?
LIMIT 1;
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db, isPathHashSeenSql, -1, &rawStmt, nullptr) !=
      SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db, stmt.get(), 1, pathHash);

  const int rc = sqlite3_step(stmt.get());
  if (rc == SQLITE_ROW) {
    return true;
  }
  if (rc == SQLITE_DONE) {
    return false;
  }

  throw std::runtime_error(std::string("Step failed: ") + sqlite3_errmsg(db));
}

size_t markEntriesDeletedById(sqlite3 *db,
                              const std::vector<std::string> &entryIds) {
  if (entryIds.empty()) {
    return 0;
  }

  static constexpr const char *markEntryDeletedSql = R"sql(
UPDATE entries
SET
  sync_state = 'deleted',
  updated_at = CURRENT_TIMESTAMP
WHERE id = ?
  AND sync_state != 'deleted';
  )sql";

  execOrThrow(db, "BEGIN TRANSACTION;");

  size_t changedCount = 0;
  try {
    sqlite3_stmt *rawStmt = nullptr;
    if (sqlite3_prepare_v2(db, markEntryDeletedSql, -1, &rawStmt, nullptr) !=
        SQLITE_OK) {
      throw std::runtime_error(std::string("Prepare failed: ") +
                               sqlite3_errmsg(db));
    }

    StmtUniquePtr stmt(rawStmt);
    for (const auto &entryId : entryIds) {
      sqlite3_reset(stmt.get());
      sqlite3_clear_bindings(stmt.get());
      bindText(db, stmt.get(), 1, entryId);

      const int rc = sqlite3_step(stmt.get());
      if (rc != SQLITE_DONE) {
        throw std::runtime_error(std::string("Step failed: ") +
                                 sqlite3_errmsg(db));
      }

      changedCount += static_cast<size_t>(sqlite3_changes(db));
    }

    execOrThrow(db, "COMMIT;");
  } catch (...) {
    sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
    throw;
  }

  return changedCount;
}

} // namespace

LocalEntryRepo::LocalEntryRepo(sqlite3 *db, LocalPathProtector &pathProtector)
    : db_(db), pathProtector_(pathProtector) {}

std::vector<EntryRecord>
LocalEntryRepo::listEntriesPendingUpload(size_t limit) const {
  if (limit == 0) {
    return {};
  }

  static constexpr const char *listEntriesPendingUploadSql = R"sql(
SELECT
  e.id,
  e.remote_id,
  e.remote_file_id,
  e.remote_folder_id,
  e.sync_root_id,
  e.remote_type,
  e.local_path,
  e.encrypted_local_path,
  e.local_path_hash,
  e.is_directory,
  e.parent_folder_id,
  e.remote_parent_folder_id,
  e.encrypted_name,
  e.local_size,
  e.local_mtime,
  e.local_hash,
  e.remote_updated_at,
  e.remote_deleted_at,
  e.remote_purged_at,
  e.last_remote_seen_at,
  e.sync_state,
  e.last_synced_at
FROM entries e
LEFT JOIN transfer_queue tq
  ON tq.entry_id = e.id
 AND tq.direction = 'upload'
 AND tq.status IN ('queued', 'running')
WHERE e.sync_state = 'pending_upload'
  AND e.is_directory = 0
  AND tq.id IS NULL
ORDER BY e.updated_at ASC, e.local_path_hash ASC
LIMIT ?;
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, listEntriesPendingUploadSql, -1, &rawStmt,
                         nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  throwIfBindFailed(db_, sqlite3_bind_int64(stmt.get(), 1,
                                            static_cast<sqlite3_int64>(limit)));

  std::vector<EntryRecord> entries;
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) {
      break;
    }
    if (rc != SQLITE_ROW) {
      throw std::runtime_error(std::string("Step failed: ") +
                               sqlite3_errmsg(db_));
    }

    entries.push_back(readEntryRecord(stmt.get()));
  }

  return entries;
}

std::optional<EntryRecord>
LocalEntryRepo::getEntryById(const std::string &entryId) const {
  static constexpr const char *getEntryByIdSql = R"sql(
SELECT
  id,
  remote_id,
  remote_file_id,
  remote_folder_id,
  sync_root_id,
  remote_type,
  local_path,
  encrypted_local_path,
  local_path_hash,
  is_directory,
  parent_folder_id,
  remote_parent_folder_id,
  encrypted_name,
  local_size,
  local_mtime,
  local_hash,
  remote_updated_at,
  remote_deleted_at,
  remote_purged_at,
  last_remote_seen_at,
  sync_state,
  last_synced_at
FROM entries
WHERE id = ?;
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, getEntryByIdSql, -1, &rawStmt, nullptr) !=
      SQLITE_OK) {
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

  return readEntryRecord(stmt.get());
}

std::string
LocalEntryRepo::computeLocalPathHash(const std::string &localPath) const {
  return pathProtector_.hashPath(localPath);
}

void LocalEntryRepo::markEntrySynced(const std::string &entryId) {
  static constexpr const char *markEntrySyncedSql = R"sql(
    UPDATE entries
    SET
      sync_state = 'synced',
      last_synced_at = CURRENT_TIMESTAMP,
      updated_at = CURRENT_TIMESTAMP
    WHERE id = ?;
      )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, markEntrySyncedSql, -1, &rawStmt, nullptr) !=
      SQLITE_OK) {
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

  if (sqlite3_changes(db_) != 1) {
    throw std::runtime_error(std::string("Changes failed") +
                             sqlite3_errmsg(db_));
  }
}

void LocalEntryRepo::markEntryUploaded(const std::string &entryId,
                                       const std::string &remoteId) {
  static constexpr const char *markEntryUploadedSql = R"sql(
    UPDATE entries
    SET
      remote_id = ?,
      sync_state = 'synced',
      last_synced_at = CURRENT_TIMESTAMP,
      updated_at = CURRENT_TIMESTAMP
    WHERE id = ?;
      )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, markEntryUploadedSql, -1, &rawStmt, nullptr) !=
      SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, remoteId);
  bindText(db_, stmt.get(), 2, entryId);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }

  if (sqlite3_changes(db_) != 1) {
    throw std::runtime_error(std::string("Changes failed") +
                             sqlite3_errmsg(db_));
  }
}

size_t
LocalEntryRepo::upsertEntries(const std::vector<EntryRecord> &entries) const {
  std::vector<EntryUpsertRecord> upsertRecords;
  upsertRecords.reserve(entries.size());
  for (const auto &entry : entries) {
    upsertRecords.push_back(EntryUpsertRecord{
        .entry = entry,
        .localPathHash = pathProtector_.hashPath(entry.localPath),
    });
  }

  return upsertScannedEntries(upsertRecords);
}

size_t LocalEntryRepo::upsertScannedEntries(
    const std::vector<EntryUpsertRecord> &entries) const {
  if (entries.empty()) {
    return 0;
  }

  static constexpr const char *upsertEntrySql = R"sql(
INSERT INTO entries (
  id, remote_id, remote_file_id, remote_folder_id, sync_root_id, remote_type,
  local_path, encrypted_local_path, local_path_hash, is_directory,
  parent_folder_id, remote_parent_folder_id, encrypted_name, local_size,
  local_mtime, local_hash, remote_updated_at, remote_deleted_at,
  remote_purged_at, last_remote_seen_at, sync_state, last_synced_at, updated_at
) VALUES (
  ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP
)
ON CONFLICT(id) DO UPDATE SET
  remote_id = excluded.remote_id,
  remote_file_id = excluded.remote_file_id,
  remote_folder_id = excluded.remote_folder_id,
  remote_type = excluded.remote_type,
  local_path = excluded.local_path,
  encrypted_local_path = excluded.encrypted_local_path,
  local_path_hash = excluded.local_path_hash,
  is_directory = excluded.is_directory,
  parent_folder_id = excluded.parent_folder_id,
  remote_parent_folder_id = excluded.remote_parent_folder_id,
  encrypted_name = excluded.encrypted_name,
  local_size = excluded.local_size,
  local_mtime = excluded.local_mtime,
  local_hash = excluded.local_hash,
  remote_updated_at = excluded.remote_updated_at,
  remote_deleted_at = excluded.remote_deleted_at,
  remote_purged_at = excluded.remote_purged_at,
  last_remote_seen_at = excluded.last_remote_seen_at,
  sync_state = excluded.sync_state,
  last_synced_at = excluded.last_synced_at,
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
    for (const auto &entryRecord : entries) {
      const auto &entry = entryRecord.entry;
      const std::string encryptedLocalPath =
          pathProtector_.encryptPath(entry.syncRootId, entry.localPath);

      sqlite3_reset(stmt.get());
      sqlite3_clear_bindings(stmt.get());

      bindText(db_, stmt.get(), 1, entry.id);
      bindOptionalText(db_, stmt.get(), 2, entry.remoteId);
      bindOptionalText(db_, stmt.get(), 3, entry.remoteFileId);
      bindOptionalText(db_, stmt.get(), 4, entry.remoteFolderId);
      bindText(db_, stmt.get(), 5, entry.syncRootId);
      bindText(db_, stmt.get(), 6, entry.remoteType);
      bindText(db_, stmt.get(), 7, entry.localPath);
      bindText(db_, stmt.get(), 8, encryptedLocalPath);
      bindText(db_, stmt.get(), 9, entryRecord.localPathHash);
      throwIfBindFailed(
          db_, sqlite3_bind_int(stmt.get(), 10, entry.isDirectory ? 1 : 0));
      bindOptionalText(db_, stmt.get(), 11, entry.parentFolderId);
      bindOptionalText(db_, stmt.get(), 12, entry.remoteParentFolderId);
      bindOptionalText(db_, stmt.get(), 13, entry.encryptedName);
      bindOptionalInt64(db_, stmt.get(), 14, entry.localSize);
      bindOptionalText(db_, stmt.get(), 15, entry.localMtime);
      bindOptionalText(db_, stmt.get(), 16, entry.localHash);
      bindOptionalText(db_, stmt.get(), 17, entry.remoteUpdatedAt);
      bindOptionalText(db_, stmt.get(), 18, entry.remoteDeletedAt);
      bindOptionalText(db_, stmt.get(), 19, entry.remotePurgedAt);
      bindOptionalText(db_, stmt.get(), 20, entry.lastRemoteSeenAt);
      bindText(db_, stmt.get(), 21, entry.syncState);
      bindOptionalText(db_, stmt.get(), 22, entry.lastSyncedAt);

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

  return entries.size();
}

size_t LocalEntryRepo::markPathDeleted(const std::string &syncRootId,
                                       const std::string &relativePath) const {
  static constexpr const char *markPathDeletedSql = R"sql(
UPDATE entries
SET sync_state = 'deleted', updated_at = CURRENT_TIMESTAMP
WHERE sync_root_id = ?
  AND local_path_hash = ?
  AND sync_state != 'deleted';
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, markPathDeletedSql, -1, &rawStmt, nullptr) !=
      SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }
  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, syncRootId);
  bindText(db_, stmt.get(), 2, pathProtector_.hashPath(relativePath));
  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }
  return static_cast<size_t>(sqlite3_changes(db_));
}

size_t
LocalEntryRepo::markSubtreeDeleted(const std::string &syncRootId,
                                   const std::string &relativeDirPath) const {
  if (relativeDirPath.empty()) {
    static constexpr const char *sql = R"sql(
UPDATE entries
SET sync_state = 'deleted', updated_at = CURRENT_TIMESTAMP
WHERE sync_root_id = ?
  AND sync_state != 'deleted';
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
    return static_cast<size_t>(sqlite3_changes(db_));
  }

  static constexpr const char *sql = R"sql(
UPDATE entries
SET sync_state = 'deleted', updated_at = CURRENT_TIMESTAMP
WHERE sync_root_id = ?
  AND sync_state != 'deleted'
  AND (local_path = ? OR local_path LIKE ? || '/%');
  )sql";
  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }
  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, syncRootId);
  bindText(db_, stmt.get(), 2, relativeDirPath);
  bindText(db_, stmt.get(), 3, relativeDirPath);
  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }
  return static_cast<size_t>(sqlite3_changes(db_));
}

size_t LocalEntryRepo::markMissingEntriesDeletedUnderPrefix(
    const std::string &syncRootId, const std::string &relativeDirPath) const {
  if (relativeDirPath.empty()) {
    return markMissingEntriesDeletedForCurrentScan(syncRootId);
  }

  static constexpr const char *sql = R"sql(
UPDATE entries
SET sync_state = 'deleted', updated_at = CURRENT_TIMESTAMP
WHERE sync_root_id = ?
  AND sync_state != 'deleted'
  AND (local_path = ? OR local_path LIKE ? || '/%')
  AND NOT EXISTS (
    SELECT 1
    FROM scan_seen_paths
    WHERE scan_seen_paths.local_path_hash = entries.local_path_hash
  );
  )sql";
  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }
  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, syncRootId);
  bindText(db_, stmt.get(), 2, relativeDirPath);
  bindText(db_, stmt.get(), 3, relativeDirPath);
  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }
  return static_cast<size_t>(sqlite3_changes(db_));
}

std::vector<EntryRecord>
LocalEntryRepo::getEntriesForSyncRoot(const std::string &syncRootId) const {
  static constexpr const char *sql = R"sql(
SELECT
  id, remote_id, remote_file_id, remote_folder_id, sync_root_id, remote_type,
  local_path, encrypted_local_path, local_path_hash, is_directory,
  parent_folder_id, remote_parent_folder_id, encrypted_name, local_size,
  local_mtime, local_hash, remote_updated_at, remote_deleted_at,
  remote_purged_at, last_remote_seen_at, sync_state, last_synced_at
FROM entries
WHERE sync_root_id = ?;
  )sql";
  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }
  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, syncRootId);
  std::vector<EntryRecord> entries;
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) {
      break;
    }
    if (rc != SQLITE_ROW) {
      throw std::runtime_error(std::string("Step failed: ") +
                               sqlite3_errmsg(db_));
    }
    entries.push_back(readEntryRecord(stmt.get()));
  }
  return entries;
}

size_t LocalEntryRepo::markMissingEntriesDeletedForCurrentScan(
    const std::string &syncRootId) const {
  static constexpr const char *sql = R"sql(
UPDATE entries
SET sync_state = 'deleted', updated_at = CURRENT_TIMESTAMP
WHERE sync_root_id = ?
  AND sync_state != 'deleted'
  AND NOT EXISTS (
    SELECT 1
    FROM scan_seen_paths
    WHERE scan_seen_paths.local_path_hash = entries.local_path_hash
  );
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
  return static_cast<size_t>(sqlite3_changes(db_));
}

std::vector<EntryRecord> LocalEntryRepo::listRemoteDeletedLocalEntries(
    const std::string &syncRootId) const {
  static constexpr const char *sql = R"sql(
SELECT
  e.id,
  e.remote_id,
  e.remote_file_id,
  e.remote_folder_id,
  e.sync_root_id,
  e.remote_type,
  e.local_path,
  e.encrypted_local_path,
  e.local_path_hash,
  e.is_directory,
  e.parent_folder_id,
  e.remote_parent_folder_id,
  e.encrypted_name,
  e.local_size,
  e.local_mtime,
  e.local_hash,
  e.remote_updated_at,
  e.remote_deleted_at,
  e.remote_purged_at,
  e.last_remote_seen_at,
  e.sync_state,
  e.last_synced_at
FROM entries e
WHERE e.sync_root_id = ?
  AND e.remote_deleted_at IS NOT NULL
  AND e.sync_state != 'deleted'
  AND e.local_path NOT LIKE '__remote__/%';
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, syncRootId);

  std::vector<EntryRecord> rows;
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_ROW) {
      rows.push_back(readEntryRecord(stmt.get()));
      continue;
    }
    if (rc == SQLITE_DONE) {
      break;
    }
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }

  return rows;
}

size_t LocalEntryRepo::markEntryDeletedById(const std::string &id) const {
  static constexpr const char *sql = R"sql(
    UPDATE entries
    SET sync_state = 'deleted',
        updated_at = CURRENT_TIMESTAMP
    WHERE id = ?;
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, id);
  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }
  return static_cast<size_t>(sqlite3_changes(db_));
}
