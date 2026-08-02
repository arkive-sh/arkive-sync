#include "repo/DirtyPathRepo.hpp"

#include "db/SqliteHelpers.hpp"
#include "fs/helpers/PathHelpers.hpp"
#include "helpers/GenUUID.hpp"
#include "repo/SyncRepo.hpp"

#include <spdlog/spdlog.h>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace {

const char *toDirtyPathEventTypeString(DirtyPathEventType type) {
  switch (type) {
  case DirtyPathEventType::Scan:
    return "scan";
  case DirtyPathEventType::Delete:
    return "delete";
  case DirtyPathEventType::FullRescan:
    return "full_rescan";
  }

  throw std::runtime_error("Unknown DirtyPathEventType");
}

DirtyPathEventType parseDirtyPathEventType(const char *value) {
  if (value == nullptr) {
    throw std::runtime_error("dirty_paths.event_type was NULL");
  }

  const std::string raw(value);
  if (raw == "scan") {
    return DirtyPathEventType::Scan;
  }
  if (raw == "delete") {
    return DirtyPathEventType::Delete;
  }
  if (raw == "full_rescan") {
    return DirtyPathEventType::FullRescan;
  }

  throw std::runtime_error("Unknown dirty_paths.event_type: " + raw);
}

DirtyPathStatus parseDirtyPathStatus(const char *value) {
  if (value == nullptr) {
    throw std::runtime_error("dirty_paths.status was NULL");
  }

  const std::string raw(value);
  if (raw == "pending") {
    return DirtyPathStatus::Pending;
  }
  if (raw == "running") {
    return DirtyPathStatus::Running;
  }
  if (raw == "done") {
    return DirtyPathStatus::Done;
  }
  if (raw == "failed") {
    return DirtyPathStatus::Failed;
  }

  throw std::runtime_error("Unknown dirty_paths.status: " + raw);
}

DirtyPath readDirtyPath(sqlite3_stmt *stmt) {
  const char *id = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
  const char *syncRootId =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
  const char *relativePath =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
  const char *eventType =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
  const char *status =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
  const char *createdAt =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5));
  const char *updatedAt =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 6));

  if (id == nullptr || syncRootId == nullptr || createdAt == nullptr ||
      updatedAt == nullptr) {
    throw std::runtime_error("dirty_paths row contained NULL value");
  }

  return DirtyPath{
      .id = id,
      .syncRootId = syncRootId,
      .relativePath = relativePath != nullptr
                          ? std::optional<std::string>(relativePath)
                          : std::nullopt,
      .eventType = parseDirtyPathEventType(eventType),
      .status = parseDirtyPathStatus(status),
      .createdAt = createdAt,
      .updatedAt = updatedAt,
  };
}

bool isArkiveTempPath(const std::filesystem::path &path) {
  const std::string name = path.filename().string();
  return name.ends_with(".tmp") && name.find(".arkive-") != std::string::npos;
}

} // namespace

DirtyPathRepo::DirtyPathRepo(sqlite3 *db) : db_(db) {
  if (db == nullptr) {
    throw std::invalid_argument(
        "DirtyPathRepo needs a valid sqlite3 connection");
  }
}

void DirtyPathRepo::record(const FileEvent &event) {
  if (event.type == FileEventType::Unknown) {
    spdlog::warn("Ignoring unknown file event for root {}", event.rootId);
    return;
  }

  if (event.type == FileEventType::Overflow) {
    for (const auto &root : SyncRepo(db_).getSyncRoots()) {
      insertFullRescan(root.Id);
    }
    return;
  }

  const auto root = SyncRepo(db_).findSyncRootById(event.rootId);
  if (!root.has_value()) {
    spdlog::warn("Ignoring file event for unknown root {}", event.rootId);
    return;
  }

  if (isArkiveTempPath(event.path) ||
      (event.oldPath.has_value() && isArkiveTempPath(*event.oldPath))) {
    spdlog::debug("Ignoring arkive temp file event type={} path={}",
                  eventTypeName(event.type), event.path.string());
    return;
  }

  const auto toRelativePath =
      [&](const std::filesystem::path &path) -> std::optional<std::string> {
    const std::filesystem::path normalizedPath = normalizeFsPath(path);
    const std::filesystem::path normalizedRoot = normalizeFsPath(root->localPath);
    const std::filesystem::path relative =
        normalizedPath.lexically_relative(normalizedRoot);

    if (relative.empty()) {
      return std::string();
    }
    if (relative == "." || *relative.begin() == "..") {
      spdlog::warn("Ignoring file event outside root {} path={}", root->Id,
                   normalizedPath.string());
      return std::nullopt;
    }

    return relative.string();
  };

  const auto insertScan = [&](const std::filesystem::path &path) {
    const auto relativePath = toRelativePath(path);
    if (!relativePath.has_value()) {
      return;
    }
    insertDirtyPath(root->Id, relativePath, DirtyPathEventType::Scan);
  };
  const auto insertDelete = [&](const std::filesystem::path &path) {
    const auto relativePath = toRelativePath(path);
    if (!relativePath.has_value()) {
      return;
    }
    insertDirtyPath(root->Id, relativePath, DirtyPathEventType::Delete);
  };

  switch (event.type) {
  case FileEventType::Created:
  case FileEventType::Modified:
  case FileEventType::AttributeChanged:
  case FileEventType::MovedTo:
    insertScan(event.path);
    return;
  case FileEventType::Deleted:
  case FileEventType::MovedFrom:
    insertDelete(event.path);
    return;
  case FileEventType::Renamed:
    if (event.oldPath.has_value()) {
      insertDelete(*event.oldPath);
    }
    insertScan(event.path);
    return;
  case FileEventType::Overflow:
  case FileEventType::Unknown:
    return;
  }
}

void DirtyPathRepo::insertFullRescan(const std::string &syncRootId) {
  insertDirtyPath(syncRootId, std::nullopt, DirtyPathEventType::FullRescan);
}

void DirtyPathRepo::insertDirtyPath(
    const std::string &syncRootId,
    const std::optional<std::string> &relativePath,
    DirtyPathEventType action) {
  static constexpr const char *pathSql = R"sql(
INSERT INTO dirty_paths (
  id,
  sync_root_id,
  relative_path,
  event_type,
  status,
  created_at,
  updated_at
) VALUES (
  ?,
  ?,
  ?,
  ?,
  'pending',
  CURRENT_TIMESTAMP,
  CURRENT_TIMESTAMP
) ON CONFLICT(sync_root_id, relative_path)
WHERE status = 'pending' AND relative_path IS NOT NULL
DO UPDATE SET
  event_type = excluded.event_type,
  updated_at = CURRENT_TIMESTAMP;
  )sql";

  static constexpr const char *fullRescanSql = R"sql(
INSERT INTO dirty_paths (
  id,
  sync_root_id,
  relative_path,
  event_type,
  status,
  created_at,
  updated_at
) VALUES (
  ?,
  ?,
  NULL,
  ?,
  'pending',
  CURRENT_TIMESTAMP,
  CURRENT_TIMESTAMP
) ON CONFLICT(sync_root_id)
WHERE status = 'pending'
  AND relative_path IS NULL
  AND event_type = 'full_rescan'
DO UPDATE SET
  updated_at = CURRENT_TIMESTAMP;
  )sql";

  if (action != DirtyPathEventType::FullRescan && !relativePath.has_value()) {
    throw std::invalid_argument("Path events require a relative path");
  }

  const char *sql =
      action == DirtyPathEventType::FullRescan ? fullRescanSql : pathSql;
  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, generateUUID());
  bindText(db_, stmt.get(), 2, syncRootId);
  if (action == DirtyPathEventType::FullRescan) {
    bindText(db_, stmt.get(), 3, toDirtyPathEventTypeString(action));
  } else {
    bindOptionalText(db_, stmt.get(), 3, relativePath);
    bindText(db_, stmt.get(), 4, toDirtyPathEventTypeString(action));
  }

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }
}

std::optional<DirtyPath>
DirtyPathRepo::claimNextPending(const std::string &syncRootId) {
  static constexpr const char *selectSql = R"sql(
SELECT
  id,
  sync_root_id,
  relative_path,
  event_type,
  status,
  created_at,
  updated_at
FROM dirty_paths
WHERE sync_root_id = ?
  AND status IN ('pending', 'failed')
ORDER BY
  CASE status
    WHEN 'pending' THEN 0
    ELSE 1
  END,
  created_at ASC,
  rowid ASC
LIMIT 1;
  )sql";

  static constexpr const char *updateSql = R"sql(
UPDATE dirty_paths
SET
  status = 'running',
  updated_at = CURRENT_TIMESTAMP
WHERE id = ?;
  )sql";

  execOrThrow(db_, "BEGIN IMMEDIATE;");
  try {
    sqlite3_stmt *rawSelectStmt = nullptr;
    if (sqlite3_prepare_v2(db_, selectSql, -1, &rawSelectStmt, nullptr) !=
        SQLITE_OK) {
      throw std::runtime_error(std::string("Prepare failed: ") +
                               sqlite3_errmsg(db_));
    }

    StmtUniquePtr selectStmt(rawSelectStmt);
    bindText(db_, selectStmt.get(), 1, syncRootId);

    const int selectRc = sqlite3_step(selectStmt.get());
    if (selectRc == SQLITE_DONE) {
      execOrThrow(db_, "COMMIT;");
      return std::nullopt;
    }
    if (selectRc != SQLITE_ROW) {
      throw std::runtime_error(std::string("Step failed: ") +
                               sqlite3_errmsg(db_));
    }

    DirtyPath dirtyPath = readDirtyPath(selectStmt.get());

    sqlite3_stmt *rawUpdateStmt = nullptr;
    if (sqlite3_prepare_v2(db_, updateSql, -1, &rawUpdateStmt, nullptr) !=
        SQLITE_OK) {
      throw std::runtime_error(std::string("Prepare failed: ") +
                               sqlite3_errmsg(db_));
    }

    StmtUniquePtr updateStmt(rawUpdateStmt);
    bindText(db_, updateStmt.get(), 1, dirtyPath.id);

    const int updateRc = sqlite3_step(updateStmt.get());
    if (updateRc != SQLITE_DONE) {
      throw std::runtime_error(std::string("Step failed: ") +
                               sqlite3_errmsg(db_));
    }

    execOrThrow(db_, "COMMIT;");
    dirtyPath.status = DirtyPathStatus::Running;
    return dirtyPath;
  } catch (...) {
    sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
    throw;
  }
}

void DirtyPathRepo::markDone(const std::string &dirtyPathId) {
  static constexpr const char *sql = R"sql(
UPDATE dirty_paths
SET
  status = 'done',
  error_message = NULL,
  updated_at = CURRENT_TIMESTAMP
WHERE id = ?;
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, dirtyPathId);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }
}

void DirtyPathRepo::markFailed(const std::string &dirtyPathId,
                               const std::string &reason) {
  static constexpr const char *sql = R"sql(
UPDATE dirty_paths
SET
  status = 'failed',
  error_message = ?,
  updated_at = CURRENT_TIMESTAMP
WHERE id = ?;
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, reason);
  bindText(db_, stmt.get(), 2, dirtyPathId);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }
}

std::vector<DirtyPath>
DirtyPathRepo::getDirtyPathsBySyncRootId(const std::string &syncRootId) {
  static constexpr const char *sql = R"sql(
SELECT
  id,
  sync_root_id,
  relative_path,
  event_type,
  status,
  created_at,
  updated_at
FROM dirty_paths
WHERE sync_root_id = ?
ORDER BY created_at ASC;
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, syncRootId);

  std::vector<DirtyPath> dirtyPaths;
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) {
      break;
    }
    if (rc != SQLITE_ROW) {
      throw std::runtime_error(std::string("Step failed: ") +
                               sqlite3_errmsg(db_));
    }

    dirtyPaths.emplace_back(readDirtyPath(stmt.get()));
  }

  return dirtyPaths;
}
