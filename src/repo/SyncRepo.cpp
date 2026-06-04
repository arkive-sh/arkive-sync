#include "repo/SyncRepo.hpp"
#include "db/SqliteHelpers.hpp"
#include "helpers/LocalPathProtector.hpp"

#include <optional>
#include <sqlite3.h>
#include <stdexcept>

namespace {

SyncRootRecord readSyncRootRecord(sqlite3_stmt *stmt, LocalPathProtector &) {
  const char *id = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
  const char *localPath =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
  const char *folderId =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));

  if (id == nullptr || localPath == nullptr) {
    throw std::invalid_argument("sync_roots row contained NULL value");
  }

  return SyncRootRecord{
      .id = id,
      .localPath = localPath,
      .folderId = folderId != nullptr ? std::optional<std::string>(folderId)
                                      : std::nullopt,
      .enabled = sqlite3_column_int(stmt, 5) != 0,
  };
}

EntryRecord readEntryRecord(sqlite3_stmt *stmt, LocalPathProtector &) {
  const char *id = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
  const char *remoteId =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
  const char *syncRootId =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
  const char *remoteType =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
  const char *localPath =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
  const char *parentFolderId =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 8));
  const char *encryptedName =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 9));
  const char *localMtime =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 11));
  const char *localHash =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 12));
  const char *remoteUpdatedAt =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 13));
  const char *syncState =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 14));
  const char *lastSyncedAt =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 15));

  if (id == nullptr || syncRootId == nullptr || remoteType == nullptr ||
      localPath == nullptr || syncState == nullptr) {
    throw std::invalid_argument("entries row contained NULL value");
  }

  return EntryRecord{
      .id = id,
      .remoteId = remoteId != nullptr ? std::optional<std::string>(remoteId)
                                      : std::nullopt,
      .syncRootId = syncRootId,
      .remoteType = remoteType,
      .localPath = localPath,
      .isDirectory = sqlite3_column_int(stmt, 7) != 0,
      .parentFolderId = parentFolderId != nullptr
                            ? std::optional<std::string>(parentFolderId)
                            : std::nullopt,
      .encryptedName = encryptedName != nullptr
                           ? std::optional<std::string>(encryptedName)
                           : std::nullopt,
      .localSize =
          sqlite3_column_type(stmt, 10) != SQLITE_NULL
              ? std::optional<int64_t>(sqlite3_column_int64(stmt, 10))
              : std::nullopt,
      .localMtime = localMtime != nullptr ? std::optional<std::string>(localMtime)
                                          : std::nullopt,
      .localHash = localHash != nullptr ? std::optional<std::string>(localHash)
                                        : std::nullopt,
      .remoteUpdatedAt = remoteUpdatedAt != nullptr
                             ? std::optional<std::string>(remoteUpdatedAt)
                             : std::nullopt,
      .syncState = syncState,
      .lastSyncedAt = lastSyncedAt != nullptr
                          ? std::optional<std::string>(lastSyncedAt)
                          : std::nullopt,
  };
}

EntryIdentity readEntryIdentity(sqlite3_stmt *stmt) {
  const char *id = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
  const char *remoteId =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
  const char *parentFolderId =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
  const char *encryptedName =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
  const char *localMtime =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 6));
  const char *localHash =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 7));
  const char *remoteUpdatedAt =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 8));
  const char *syncState =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 9));
  const char *lastSyncedAt =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 10));

  if (id == nullptr || syncState == nullptr) {
    throw std::invalid_argument("entries scan row contained NULL value");
  }

  return EntryIdentity{
      .id = id,
      .remoteId = remoteId != nullptr ? std::optional<std::string>(remoteId)
                                      : std::nullopt,
      .isDirectory = sqlite3_column_int(stmt, 2) != 0,
      .parentFolderId = parentFolderId != nullptr
                            ? std::optional<std::string>(parentFolderId)
                            : std::nullopt,
      .encryptedName = encryptedName != nullptr
                           ? std::optional<std::string>(encryptedName)
                           : std::nullopt,
      .localSize =
          sqlite3_column_type(stmt, 5) != SQLITE_NULL
              ? std::optional<int64_t>(sqlite3_column_int64(stmt, 5))
              : std::nullopt,
      .localMtime = localMtime != nullptr ? std::optional<std::string>(localMtime)
                                          : std::nullopt,
      .localHash = localHash != nullptr ? std::optional<std::string>(localHash)
                                        : std::nullopt,
      .remoteUpdatedAt = remoteUpdatedAt != nullptr
                             ? std::optional<std::string>(remoteUpdatedAt)
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

SyncScanSession::SyncScanSession(sqlite3 *db)
    : db_(db), lookupStmt_(nullptr), markSeenPathStmt_(nullptr) {
  execOrThrow(db_, R"sql(
CREATE TEMP TABLE IF NOT EXISTS scan_seen_paths (
  local_path_hash TEXT PRIMARY KEY
);
DELETE FROM scan_seen_paths;
  )sql");

  static constexpr const char *getEntryIdentityByLocalPathHashSql = R"sql(
SELECT
  id,
  remote_id,
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
WHERE sync_root_id = ?
  AND local_path_hash = ?;
  )sql";

  sqlite3_stmt *rawLookupStmt = nullptr;
  if (sqlite3_prepare_v2(db_, getEntryIdentityByLocalPathHashSql, -1,
                         &rawLookupStmt,
                         nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }
  lookupStmt_.reset(rawLookupStmt);

  static constexpr const char *markPathSeenSql = R"sql(
INSERT OR IGNORE INTO scan_seen_paths (
  local_path_hash
) VALUES (?);
  )sql";

  sqlite3_stmt *rawMarkSeenPathStmt = nullptr;
  if (sqlite3_prepare_v2(db_, markPathSeenSql, -1, &rawMarkSeenPathStmt,
                         nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }
  markSeenPathStmt_.reset(rawMarkSeenPathStmt);
}

std::optional<EntryIdentity>
SyncScanSession::findEntryIdentityByPathHash(
    const std::string &syncRootId, const std::string &localPathHash) const {
  sqlite3_reset(lookupStmt_.get());
  sqlite3_clear_bindings(lookupStmt_.get());
  bindText(db_, lookupStmt_.get(), 1, syncRootId);
  bindText(db_, lookupStmt_.get(), 2, localPathHash);

  const int rc = sqlite3_step(lookupStmt_.get());
  if (rc == SQLITE_DONE) {
    return std::nullopt;
  }
  if (rc != SQLITE_ROW) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }

  return readEntryIdentity(lookupStmt_.get());
}

void SyncScanSession::recordSeenPath(const std::string &localPathHash) const {
  sqlite3_reset(markSeenPathStmt_.get());
  sqlite3_clear_bindings(markSeenPathStmt_.get());
  bindText(db_, markSeenPathStmt_.get(), 1, localPathHash);

  const int rc = sqlite3_step(markSeenPathStmt_.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }
}

SyncRepo::SyncRepo(sqlite3 *db, LocalPathProtector &pathProtector)
    : db_(db), pathProtector_(pathProtector) {
  if (db == nullptr) {
    throw std::invalid_argument("Sync Repo needs a valid sqlite3 connection");
  }
}

std::optional<SyncRootRecord>
SyncRepo::getSyncRootById(const std::string &syncRootId) const {
  static constexpr const char *getSyncRootByIdSql = R"sql(
SELECT
  id,
  local_path,
  encrypted_local_path,
  local_path_hash,
  folder_id,
  enabled
FROM sync_roots
WHERE id = ?;
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, getSyncRootByIdSql, -1, &rawStmt, nullptr) !=
      SQLITE_OK) {
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

  return readSyncRootRecord(stmt.get(), pathProtector_);
}

std::optional<SyncRootRecord>
SyncRepo::getSyncRootByLocalPath(const std::string &localPath) const {
  static constexpr const char *getSyncRootSql = R"sql(
SELECT
  id,
  local_path,
  encrypted_local_path,
  local_path_hash,
  folder_id,
  enabled
FROM sync_roots
WHERE local_path_hash = ?;
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, getSyncRootSql, -1, &rawStmt, nullptr) !=
      SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, pathProtector_.hashPath(localPath));

  const int rc = sqlite3_step(stmt.get());
  if (rc == SQLITE_DONE) {
    return std::nullopt;
  }
  if (rc != SQLITE_ROW) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }

  return readSyncRootRecord(stmt.get(), pathProtector_);
}

std::vector<SyncRootRecord> SyncRepo::getSyncRoots() const {
  static constexpr const char *getSyncRootsSql = R"sql(
SELECT
  id,
  local_path,
  encrypted_local_path,
  local_path_hash,
  folder_id,
  enabled
FROM sync_roots
ORDER BY created_at ASC, local_path ASC;
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, getSyncRootsSql, -1, &rawStmt, nullptr) !=
      SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  std::vector<SyncRootRecord> roots;

  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) {
      break;
    }
    if (rc != SQLITE_ROW) {
      throw std::runtime_error(std::string("Step failed: ") +
                               sqlite3_errmsg(db_));
    }

    roots.push_back(readSyncRootRecord(stmt.get(), pathProtector_));
  }

  return roots;
}

std::vector<EntryRecord> SyncRepo::listEntriesPendingUpload(size_t limit) const {
  if (limit == 0) {
    return {};
  }

  static constexpr const char *listEntriesPendingUploadSql = R"sql(
SELECT
  e.id,
  e.remote_id,
  e.sync_root_id,
  e.remote_type,
  e.local_path,
  e.encrypted_local_path,
  e.local_path_hash,
  e.is_directory,
  e.parent_folder_id,
  e.encrypted_name,
  e.local_size,
  e.local_mtime,
  e.local_hash,
  e.remote_updated_at,
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
  throwIfBindFailed(db_,
                    sqlite3_bind_int64(stmt.get(), 1,
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

    entries.push_back(readEntryRecord(stmt.get(), pathProtector_));
  }

  return entries;
}

std::optional<EntryRecord>
SyncRepo::getEntryById(const std::string &entryId) const {
  static constexpr const char *getEntryByIdSql = R"sql(
SELECT
  id,
  remote_id,
  sync_root_id,
  remote_type,
  local_path,
  encrypted_local_path,
  local_path_hash,
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

  return readEntryRecord(stmt.get(), pathProtector_);
}

SyncScanSession SyncRepo::beginScan() const {
  return SyncScanSession(db_);
}

std::string SyncRepo::computeLocalPathHash(const std::string &localPath) const {
  return pathProtector_.hashPath(localPath);
}

void SyncRepo::markEntrySynced(const std::string &entryId) {
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

void SyncRepo::markEntryUploaded(const std::string &entryId,
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

void SyncRepo::upsertSyncRoot(const SyncRootRecord &syncRoot) const {
  static constexpr const char *upsertSyncRootSql = R"sql(
INSERT INTO sync_roots (
  id,
  local_path,
  encrypted_local_path,
  local_path_hash,
  folder_id,
  enabled,
  created_at
) VALUES (
  ?,
  ?,
  ?,
  ?,
  ?,
  ?,
  CURRENT_TIMESTAMP
)
ON CONFLICT(id) DO UPDATE SET
  local_path = excluded.local_path,
  encrypted_local_path = excluded.encrypted_local_path,
  local_path_hash = excluded.local_path_hash,
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
  const std::string localPathHash = pathProtector_.hashPath(syncRoot.localPath);
  const std::string encryptedLocalPath =
      pathProtector_.encryptPath(std::string("sync-root:") + syncRoot.id,
                                 syncRoot.localPath);
  bindText(db_, stmt.get(), 1, syncRoot.id);
  bindText(db_, stmt.get(), 2, syncRoot.localPath);
  bindText(db_, stmt.get(), 3, encryptedLocalPath);
  bindText(db_, stmt.get(), 4, localPathHash);
  bindOptionalText(db_, stmt.get(), 5, syncRoot.folderId);
  throwIfBindFailed(db_,
                    sqlite3_bind_int(stmt.get(), 6, syncRoot.enabled ? 1 : 0));

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }
}

size_t SyncRepo::upsertEntries(const std::vector<EntryRecord> &entries) const {
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

size_t
SyncRepo::upsertScannedEntries(const std::vector<EntryUpsertRecord> &entries) const {
  if (entries.empty()) {
    return 0;
  }

  static constexpr const char *upsertEntrySql = R"sql(
INSERT INTO entries (
  id,
  remote_id,
  sync_root_id,
  remote_type,
  local_path,
  encrypted_local_path,
  local_path_hash,
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
  ?,
  ?,
  CURRENT_TIMESTAMP
)
ON CONFLICT(id) DO UPDATE SET
  remote_type = excluded.remote_type,
  local_path = excluded.local_path,
  encrypted_local_path = excluded.encrypted_local_path,
  local_path_hash = excluded.local_path_hash,
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
    for (const auto &entryRecord : entries) {
      const auto &entry = entryRecord.entry;
      const std::string encryptedLocalPath =
          pathProtector_.encryptPath(entry.syncRootId, entry.localPath);

      sqlite3_reset(stmt.get());
      sqlite3_clear_bindings(stmt.get());

      bindText(db_, stmt.get(), 1, entry.id);
      bindOptionalText(db_, stmt.get(), 2, entry.remoteId);
      bindText(db_, stmt.get(), 3, entry.syncRootId);
      bindText(db_, stmt.get(), 4, entry.remoteType);
      bindText(db_, stmt.get(), 5, entry.localPath);
      bindText(db_, stmt.get(), 6, encryptedLocalPath);
      bindText(db_, stmt.get(), 7, entryRecord.localPathHash);
      throwIfBindFailed(
          db_, sqlite3_bind_int(stmt.get(), 8, entry.isDirectory ? 1 : 0));
      bindOptionalText(db_, stmt.get(), 9, entry.parentFolderId);
      bindOptionalText(db_, stmt.get(), 10, entry.encryptedName);
      bindOptionalInt64(db_, stmt.get(), 11, entry.localSize);
      bindOptionalText(db_, stmt.get(), 12, entry.localMtime);
      bindOptionalText(db_, stmt.get(), 13, entry.localHash);
      bindOptionalText(db_, stmt.get(), 14, entry.remoteUpdatedAt);
      bindText(db_, stmt.get(), 15, entry.syncState);
      bindOptionalText(db_, stmt.get(), 16, entry.lastSyncedAt);

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

size_t SyncRepo::markPathDeleted(const std::string &syncRootId,
                                 const std::string &relativePath) const {
  static constexpr const char *markPathDeletedSql = R"sql(
UPDATE entries
SET
  sync_state = 'deleted',
  updated_at = CURRENT_TIMESTAMP
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

size_t SyncRepo::markSubtreeDeleted(const std::string &syncRootId,
                                    const std::string &relativeDirPath) const {
  if (relativeDirPath.empty()) {
    static constexpr const char *markRootDeletedSql = R"sql(
UPDATE entries
SET
  sync_state = 'deleted',
  updated_at = CURRENT_TIMESTAMP
WHERE sync_root_id = ?
  AND sync_state != 'deleted';
  )sql";

    sqlite3_stmt *rawStmt = nullptr;
    if (sqlite3_prepare_v2(db_, markRootDeletedSql, -1, &rawStmt, nullptr) !=
        SQLITE_OK) {
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

  static constexpr const char *markSubtreeDeletedSql = R"sql(
UPDATE entries
SET
  sync_state = 'deleted',
  updated_at = CURRENT_TIMESTAMP
WHERE sync_root_id = ?
  AND sync_state != 'deleted'
  AND (
    local_path = ?
    OR local_path LIKE ? || '/%'
  );
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, markSubtreeDeletedSql, -1, &rawStmt, nullptr) !=
      SQLITE_OK) {
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

size_t SyncRepo::markMissingEntriesDeletedUnderPrefix(
    const std::string &syncRootId, const std::string &relativeDirPath) const {
  if (relativeDirPath.empty()) {
    static constexpr const char *markMissingEntriesDeletedRootSql = R"sql(
UPDATE entries
SET
  sync_state = 'deleted',
  updated_at = CURRENT_TIMESTAMP
WHERE sync_root_id = ?
  AND sync_state != 'deleted'
  AND NOT EXISTS (
    SELECT 1
    FROM scan_seen_paths
    WHERE scan_seen_paths.local_path_hash = entries.local_path_hash
  );
  )sql";

    sqlite3_stmt *rawStmt = nullptr;
    if (sqlite3_prepare_v2(db_, markMissingEntriesDeletedRootSql, -1, &rawStmt,
                           nullptr) != SQLITE_OK) {
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

  static constexpr const char *markMissingEntriesDeletedUnderPrefixSql = R"sql(
UPDATE entries
SET
  sync_state = 'deleted',
  updated_at = CURRENT_TIMESTAMP
WHERE sync_root_id = ?
  AND sync_state != 'deleted'
  AND (
    local_path = ?
    OR local_path LIKE ? || '/%'
  )
  AND NOT EXISTS (
    SELECT 1
    FROM scan_seen_paths
    WHERE scan_seen_paths.local_path_hash = entries.local_path_hash
  );
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, markMissingEntriesDeletedUnderPrefixSql, -1,
                         &rawStmt, nullptr) != SQLITE_OK) {
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
SyncRepo::getEntriesForSyncRoot(const std::string &syncRootId) const {
  static constexpr const char *getEntriesSql = R"sql(
SELECT
  id,
  remote_id,
  sync_root_id,
  remote_type,
  local_path,
  encrypted_local_path,
  local_path_hash,
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

    entries.push_back(readEntryRecord(stmt.get(), pathProtector_));
  }

  return entries;
}

size_t SyncRepo::markMissingEntriesDeletedForCurrentScan(
    const std::string &syncRootId) const {
  static constexpr const char *markDeletedSql = R"sql(
UPDATE entries
SET
  sync_state = 'deleted',
  updated_at = CURRENT_TIMESTAMP
WHERE sync_root_id = ?
  AND sync_state != 'deleted'
  AND NOT EXISTS (
    SELECT 1
    FROM scan_seen_paths
    WHERE scan_seen_paths.local_path_hash = entries.local_path_hash
  );
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, markDeletedSql, -1, &rawStmt, nullptr) !=
      SQLITE_OK) {
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

void SyncRepo::releaseMemory() const { ::releaseMemory(db_); }
