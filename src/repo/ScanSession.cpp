#include "repo/ScanSession.hpp"

#include "db/SqliteHelpers.hpp"

#include <sqlite3.h>
#include <stdexcept>

namespace {

EntryIdentity readEntryIdentity(sqlite3_stmt *stmt) {
  const char *id = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
  const char *remoteId =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
  const char *remoteFileId =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
  const char *remoteFolderId =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
  const char *parentFolderId =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5));
  const char *remoteParentFolderId =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 6));
  const char *encryptedName =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 7));
  const char *localMtime =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 9));
  const char *localHash =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 10));
  const char *remoteUpdatedAt =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 11));
  const char *remoteDeletedAt =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 12));
  const char *remotePurgedAt =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 13));
  const char *lastRemoteSeenAt =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 14));
  const char *syncState =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 15));
  const char *lastSyncedAt =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 16));

  if (id == nullptr || syncState == nullptr) {
    throw std::invalid_argument("entries scan row contained NULL value");
  }

  return EntryIdentity{
      .id = id,
      .remoteId = remoteId != nullptr ? std::optional<std::string>(remoteId)
                                      : std::nullopt,
      .remoteFileId = remoteFileId != nullptr
                          ? std::optional<std::string>(remoteFileId)
                          : std::nullopt,
      .remoteFolderId = remoteFolderId != nullptr
                            ? std::optional<std::string>(remoteFolderId)
                            : std::nullopt,
      .isDirectory = sqlite3_column_int(stmt, 4) != 0,
      .parentFolderId = parentFolderId != nullptr
                            ? std::optional<std::string>(parentFolderId)
                            : std::nullopt,
      .remoteParentFolderId = remoteParentFolderId != nullptr
                                  ? std::optional<std::string>(remoteParentFolderId)
                                  : std::nullopt,
      .encryptedName = encryptedName != nullptr
                           ? std::optional<std::string>(encryptedName)
                           : std::nullopt,
      .localSize =
          sqlite3_column_type(stmt, 8) != SQLITE_NULL
              ? std::optional<int64_t>(sqlite3_column_int64(stmt, 8))
              : std::nullopt,
      .localMtime = localMtime != nullptr ? std::optional<std::string>(localMtime)
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
  remote_file_id,
  remote_folder_id,
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
WHERE sync_root_id = ?
  AND local_path_hash = ?;
  )sql";

  sqlite3_stmt *rawLookupStmt = nullptr;
  if (sqlite3_prepare_v2(db_, getEntryIdentityByLocalPathHashSql, -1,
                         &rawLookupStmt, nullptr) != SQLITE_OK) {
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
