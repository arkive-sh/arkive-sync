#include "repo/RemoteEntryRepo.hpp"

#include "db/SqliteHelpers.hpp"
#include "helpers/LocalPathProtector.hpp"

#include <optional>
#include <sqlite3.h>
#include <stdexcept>

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
      .remoteParentFolderId = remoteParentFolderId != nullptr
                                  ? std::optional<std::string>(remoteParentFolderId)
                                  : std::nullopt,
      .encryptedName = encryptedName != nullptr
                           ? std::optional<std::string>(encryptedName)
                           : std::nullopt,
      .localSize =
          sqlite3_column_type(stmt, 13) != SQLITE_NULL
              ? std::optional<int64_t>(sqlite3_column_int64(stmt, 13))
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

std::optional<EntryRecord>
findEntryByRemoteObjectId(sqlite3 *db, const std::string &syncRootId,
                          const SyncEntryResponse &remoteEntry) {
  static constexpr const char *findEntryByRemoteObjectIdSql = R"sql(
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
WHERE sync_root_id = ?
  AND (
    remote_id = ?
    OR remote_file_id = ?
    OR remote_folder_id = ?
  )
LIMIT 1;
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db, findEntryByRemoteObjectIdSql, -1, &rawStmt,
                         nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db, stmt.get(), 1, syncRootId);
  bindText(db, stmt.get(), 2, remoteEntry.id);
  bindText(db, stmt.get(), 3, remoteEntry.id);
  bindText(db, stmt.get(), 4, remoteEntry.id);

  const int rc = sqlite3_step(stmt.get());
  if (rc == SQLITE_DONE) {
    return std::nullopt;
  }
  if (rc != SQLITE_ROW) {
    throw std::runtime_error(std::string("Step failed: ") + sqlite3_errmsg(db));
  }

  return readEntryRecord(stmt.get());
}

std::string buildRemotePlaceholderPath(const SyncEntryResponse &entry) {
  return std::string("__remote__/") + entry.type + "/" + entry.id;
}

bool sameRemoteMetadata(const EntryRecord &existing, const EntryRecord &updated) {
  return existing.remoteId == updated.remoteId &&
         existing.remoteFileId == updated.remoteFileId &&
         existing.remoteFolderId == updated.remoteFolderId &&
         existing.remoteType == updated.remoteType &&
         existing.isDirectory == updated.isDirectory &&
         existing.remoteParentFolderId == updated.remoteParentFolderId &&
         existing.encryptedName == updated.encryptedName &&
         existing.remoteUpdatedAt == updated.remoteUpdatedAt &&
         existing.remoteDeletedAt == updated.remoteDeletedAt &&
         existing.remotePurgedAt == updated.remotePurgedAt &&
         existing.lastRemoteSeenAt == updated.lastRemoteSeenAt &&
         existing.syncState == updated.syncState;
}

} // namespace

RemoteEntryRepo::RemoteEntryRepo(sqlite3 *db, LocalPathProtector &pathProtector)
    : db_(db), pathProtector_(pathProtector) {}

RemoteEntryUpsertAction
RemoteEntryRepo::upsertRemoteEntry(const std::string &syncRootId,
                                   const SyncEntryResponse &entry) const {
  const std::optional<EntryRecord> existingEntry =
      findEntryByRemoteObjectId(db_, syncRootId, entry);

  const std::string localPath = existingEntry.has_value()
                                    ? existingEntry->localPath
                                    : buildRemotePlaceholderPath(entry);
  const std::string localPathHash = pathProtector_.hashPath(localPath);

  std::string syncState = entry.deletedAt.has_value() ? "deleted" : "synced";
  if (existingEntry.has_value() && existingEntry->syncState == "pending_upload" &&
      !entry.deletedAt.has_value()) {
    syncState = existingEntry->syncState;
  }

  EntryRecord updatedEntry{
      .id = existingEntry.has_value() ? existingEntry->id : entry.id,
      .remoteId = entry.type == "file"
                      ? std::optional<std::string>(entry.id)
                      : existingEntry.has_value() ? existingEntry->remoteId
                                                  : std::nullopt,
      .remoteFileId = entry.type == "file"
                          ? std::optional<std::string>(entry.id)
                          : std::nullopt,
      .remoteFolderId = entry.type == "folder"
                            ? std::optional<std::string>(entry.id)
                            : std::nullopt,
      .syncRootId = syncRootId,
      .remoteType = entry.type,
      .localPath = localPath,
      .isDirectory = entry.type == "folder",
      .parentFolderId =
          existingEntry.has_value() ? existingEntry->parentFolderId : std::nullopt,
      .remoteParentFolderId = entry.parentFolderId,
      .encryptedName = entry.encryptedName,
      .localSize = existingEntry.has_value() ? existingEntry->localSize
                                             : std::nullopt,
      .localMtime = existingEntry.has_value() ? existingEntry->localMtime
                                              : std::nullopt,
      .localHash = existingEntry.has_value() ? existingEntry->localHash
                                             : std::nullopt,
      .remoteUpdatedAt = entry.updatedAt,
      .remoteDeletedAt = entry.deletedAt,
      .remotePurgedAt = entry.purgedAt,
      .lastRemoteSeenAt = entry.updatedAt,
      .syncState = syncState,
      .lastSyncedAt = existingEntry.has_value() ? existingEntry->lastSyncedAt
                                                : std::nullopt,
  };

  if (existingEntry.has_value() && sameRemoteMetadata(*existingEntry, updatedEntry)) {
    return RemoteEntryUpsertAction::Unchanged;
  }

  static constexpr const char *upsertEntrySql = R"sql(
INSERT INTO entries (
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
  ?,
  ?,
  ?,
  ?,
  ?,
  ?,
  CURRENT_TIMESTAMP
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

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, upsertEntrySql, -1, &rawStmt, nullptr) !=
      SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  const std::string encryptedLocalPath =
      pathProtector_.encryptPath(updatedEntry.syncRootId, updatedEntry.localPath);

  bindText(db_, stmt.get(), 1, updatedEntry.id);
  bindOptionalText(db_, stmt.get(), 2, updatedEntry.remoteId);
  bindOptionalText(db_, stmt.get(), 3, updatedEntry.remoteFileId);
  bindOptionalText(db_, stmt.get(), 4, updatedEntry.remoteFolderId);
  bindText(db_, stmt.get(), 5, updatedEntry.syncRootId);
  bindText(db_, stmt.get(), 6, updatedEntry.remoteType);
  bindText(db_, stmt.get(), 7, updatedEntry.localPath);
  bindText(db_, stmt.get(), 8, encryptedLocalPath);
  bindText(db_, stmt.get(), 9, localPathHash);
  throwIfBindFailed(
      db_, sqlite3_bind_int(stmt.get(), 10, updatedEntry.isDirectory ? 1 : 0));
  bindOptionalText(db_, stmt.get(), 11, updatedEntry.parentFolderId);
  bindOptionalText(db_, stmt.get(), 12, updatedEntry.remoteParentFolderId);
  bindOptionalText(db_, stmt.get(), 13, updatedEntry.encryptedName);
  bindOptionalInt64(db_, stmt.get(), 14, updatedEntry.localSize);
  bindOptionalText(db_, stmt.get(), 15, updatedEntry.localMtime);
  bindOptionalText(db_, stmt.get(), 16, updatedEntry.localHash);
  bindOptionalText(db_, stmt.get(), 17, updatedEntry.remoteUpdatedAt);
  bindOptionalText(db_, stmt.get(), 18, updatedEntry.remoteDeletedAt);
  bindOptionalText(db_, stmt.get(), 19, updatedEntry.remotePurgedAt);
  bindOptionalText(db_, stmt.get(), 20, updatedEntry.lastRemoteSeenAt);
  bindText(db_, stmt.get(), 21, updatedEntry.syncState);
  bindOptionalText(db_, stmt.get(), 22, updatedEntry.lastSyncedAt);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }

  if (entry.deletedAt.has_value()) {
    return RemoteEntryUpsertAction::Deleted;
  }
  if (!existingEntry.has_value()) {
    return RemoteEntryUpsertAction::Created;
  }
  return RemoteEntryUpsertAction::Updated;
}
