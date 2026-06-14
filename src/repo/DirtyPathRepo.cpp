#include "repo/DirtyPathRepo.hpp"

#include "db/SqliteHelpers.hpp"

#include <stdexcept>

namespace {

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

} // namespace

DirtyPathRepo::DirtyPathRepo(sqlite3 *db) : db_(db) {
  if (db == nullptr) {
    throw std::invalid_argument(
        "DirtyPathRepo needs a valid sqlite3 connection");
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
