#include "repo/SyncRootRepo.hpp"

#include "db/SqliteHelpers.hpp"
#include "helpers/LocalPathProtector.hpp"

#include <sqlite3.h>
#include <stdexcept>

namespace {

SyncRootRecord readSyncRootRecord(sqlite3_stmt *stmt) {
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

} // namespace

SyncRootRepo::SyncRootRepo(sqlite3 *db, LocalPathProtector &pathProtector)
    : db_(db), pathProtector_(pathProtector) {}

std::optional<SyncRootRecord>
SyncRootRepo::getSyncRootById(const std::string &syncRootId) const {
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

  return readSyncRootRecord(stmt.get());
}

std::optional<SyncRootRecord>
SyncRootRepo::getSyncRootByLocalPath(const std::string &localPath) const {
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

  return readSyncRootRecord(stmt.get());
}

std::vector<SyncRootRecord> SyncRootRepo::getSyncRoots() const {
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

    roots.push_back(readSyncRootRecord(stmt.get()));
  }

  return roots;
}

void SyncRootRepo::upsertSyncRoot(const SyncRootRecord &syncRoot) const {
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
  throwIfBindFailed(
      db_, sqlite3_bind_int(stmt.get(), 6, syncRoot.enabled ? 1 : 0));

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }
}
