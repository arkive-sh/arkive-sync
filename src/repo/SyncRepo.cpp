#include "repo/SyncRepo.hpp"
#include "db/SqliteHelpers.hpp"

#include <sqlite3.h>
#include <unordered_set>
#include <stdexcept>

SyncRepo::SyncRepo(sqlite3 *db) : db_(db) {
  if (db == nullptr) {
    throw std::invalid_argument("Sync Repo needs a valid sqlite3 connection");
  }
}

std::optional<SyncRootRecord>
SyncRepo::getSyncRootByLocalPath(const std::string &localPath) const {
  static constexpr const char *getSyncRootSql = R"sql(
SELECT
  id,
  local_path,
  folder_id,
  enabled
FROM sync_roots
WHERE local_path = ?;
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, getSyncRootSql, -1, &rawStmt, nullptr) !=
      SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, localPath);

  const int rc = sqlite3_step(stmt.get());
  if (rc == SQLITE_DONE) {
    return std::nullopt;
  }
  if (rc != SQLITE_ROW) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }

  const char *id =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt.get(), 0));
  const char *storedLocalPath =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt.get(), 1));
  const char *folderId =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt.get(), 2));

  if (id == nullptr || storedLocalPath == nullptr) {
    throw std::invalid_argument("sync_roots row contained NULL value");
  }

  return SyncRootRecord{
      .id = id,
      .localPath = storedLocalPath,
      .folderId = folderId != nullptr ? std::optional<std::string>(folderId)
                                      : std::nullopt,
      .enabled = sqlite3_column_int(stmt.get(), 3) != 0,
  };
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
  sync_root_id,
  remote_type,
  local_path,
  is_directory,
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
  ?,
  ?,
  CURRENT_TIMESTAMP
)
ON CONFLICT(sync_root_id, local_path) DO UPDATE SET
  remote_type = excluded.remote_type,
  is_directory = excluded.is_directory,
  local_size = excluded.local_size,
  local_mtime = excluded.local_mtime,
  local_hash = excluded.local_hash,
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
      bindText(db_, stmt.get(), 3, entry.syncRootId);
      bindText(db_, stmt.get(), 4, entry.remoteType);
      bindText(db_, stmt.get(), 5, entry.localPath);
      throwIfBindFailed(db_, sqlite3_bind_int(stmt.get(), 6,
                                              entry.isDirectory ? 1 : 0));
      bindOptionalText(db_, stmt.get(), 7, entry.parentFolderId);
      bindOptionalText(db_, stmt.get(), 8, entry.encryptedName);
      bindOptionalInt64(db_, stmt.get(), 9, entry.localSize);
      bindOptionalText(db_, stmt.get(), 10, entry.localMtime);
      bindOptionalText(db_, stmt.get(), 11, entry.localHash);
      bindOptionalText(db_, stmt.get(), 12, entry.remoteUpdatedAt);
      bindText(db_, stmt.get(), 13, entry.syncState);
      bindOptionalText(db_, stmt.get(), 14, entry.lastSyncedAt);

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

std::vector<EntryRecord>
SyncRepo::getEntriesForSyncRoot(const std::string &syncRootId) const {
  static constexpr const char *getEntriesSql = R"sql(
SELECT
  id,
  remote_id,
  sync_root_id,
  remote_type,
  local_path,
  is_directory,
  parent_folder_id,
  encrypted_name,
  local_size,
  local_mtime,
  local_hash,
  remote_updated_at,
  sync_state,
  last_synced_at
FROM entries
WHERE sync_root_id = ?;
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, getEntriesSql, -1, &rawStmt, nullptr) !=
      SQLITE_OK) {
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

    const char *id =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt.get(), 0));
    const char *remoteId =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt.get(), 1));
    const char *storedSyncRootId =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt.get(), 2));
    const char *remoteType =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt.get(), 3));
    const char *localPath =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt.get(), 4));
    const char *parentFolderId =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt.get(), 6));
    const char *encryptedName =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt.get(), 7));
    const char *localMtime =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt.get(), 9));
    const char *localHash =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt.get(), 10));
    const char *remoteUpdatedAt =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt.get(), 11));
    const char *syncState =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt.get(), 12));
    const char *lastSyncedAt =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt.get(), 13));

    if (id == nullptr || storedSyncRootId == nullptr || remoteType == nullptr ||
        localPath == nullptr || syncState == nullptr) {
      throw std::invalid_argument("entries row contained NULL value");
    }

    entries.push_back(EntryRecord{
        .id = id,
        .remoteId = remoteId != nullptr ? std::optional<std::string>(remoteId)
                                        : std::nullopt,
        .syncRootId = storedSyncRootId,
        .remoteType = remoteType,
        .localPath = localPath,
        .isDirectory = sqlite3_column_int(stmt.get(), 5) != 0,
        .parentFolderId =
            parentFolderId != nullptr ? std::optional<std::string>(parentFolderId)
                                      : std::nullopt,
        .encryptedName =
            encryptedName != nullptr ? std::optional<std::string>(encryptedName)
                                     : std::nullopt,
        .localSize = sqlite3_column_type(stmt.get(), 8) != SQLITE_NULL
                         ? std::optional<int64_t>(sqlite3_column_int64(stmt.get(), 8))
                         : std::nullopt,
        .localMtime = localMtime != nullptr
                          ? std::optional<std::string>(localMtime)
                          : std::nullopt,
        .localHash = localHash != nullptr ? std::optional<std::string>(localHash)
                                          : std::nullopt,
        .remoteUpdatedAt =
            remoteUpdatedAt != nullptr ? std::optional<std::string>(remoteUpdatedAt)
                                       : std::nullopt,
        .syncState = syncState,
        .lastSyncedAt = lastSyncedAt != nullptr
                            ? std::optional<std::string>(lastSyncedAt)
                            : std::nullopt,
    });
  }

  return entries;
}

void SyncRepo::markMissingEntriesDeleted(
    const std::string &syncRootId,
    const std::vector<std::string> &presentPaths) const {
  const auto existingEntries = getEntriesForSyncRoot(syncRootId);
  std::unordered_set<std::string> presentPathSet(presentPaths.begin(),
                                                 presentPaths.end());

  static constexpr const char *markDeletedSql = R"sql(
UPDATE entries
SET
  sync_state = 'deleted',
  updated_at = CURRENT_TIMESTAMP
WHERE sync_root_id = ?
  AND local_path = ?
  AND sync_state != 'deleted';
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, markDeletedSql, -1, &rawStmt, nullptr) !=
      SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  for (const auto &entry : existingEntries) {
    if (presentPathSet.contains(entry.localPath)) {
      continue;
    }

    sqlite3_reset(stmt.get());
    sqlite3_clear_bindings(stmt.get());
    bindText(db_, stmt.get(), 1, syncRootId);
    bindText(db_, stmt.get(), 2, entry.localPath);

    const int rc = sqlite3_step(stmt.get());
    if (rc != SQLITE_DONE) {
      throw std::runtime_error(std::string("Step failed: ") +
                               sqlite3_errmsg(db_));
    }
  }
}

size_t SyncRepo::enqueuePendingUploads(const std::string &syncRootId) const {
  static constexpr const char *enqueueSql = R"sql(
INSERT INTO transfer_queue (
  id,
  entry_id,
  direction,
  status,
  local_path,
  remote_id,
  folder_id,
  bytes_total,
  bytes_done,
  error_message,
  retry_count,
  created_at,
  updated_at
)
SELECT
  ?,
  e.id,
  'upload',
  'pending',
  e.local_path,
  e.remote_id,
  e.parent_folder_id,
  e.local_size,
  0,
  NULL,
  0,
  CURRENT_TIMESTAMP,
  CURRENT_TIMESTAMP
FROM entries e
WHERE e.id = ?
  AND e.sync_root_id = ?
  AND e.sync_state = 'pending_upload'
  AND e.is_directory = 0
  AND NOT EXISTS (
    SELECT 1
    FROM transfer_queue tq
    WHERE tq.entry_id = e.id
      AND tq.direction = 'upload'
      AND tq.status IN ('pending', 'in_progress')
  );
  )sql";

  const auto pendingEntries = getEntriesForSyncRoot(syncRootId);
  size_t insertedCount = 0;

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, enqueueSql, -1, &rawStmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  for (const auto &entry : pendingEntries) {
    if (entry.syncState != "pending_upload" || entry.isDirectory) {
      continue;
    }

    sqlite3_reset(stmt.get());
    sqlite3_clear_bindings(stmt.get());
    bindText(db_, stmt.get(), 1, entry.id + "-upload");
    bindText(db_, stmt.get(), 2, entry.id);
    bindText(db_, stmt.get(), 3, syncRootId);

    const int rc = sqlite3_step(stmt.get());
    if (rc != SQLITE_DONE) {
      throw std::runtime_error(std::string("Step failed: ") +
                               sqlite3_errmsg(db_));
    }

    insertedCount += static_cast<size_t>(sqlite3_changes(db_));
  }

  return insertedCount;
}
