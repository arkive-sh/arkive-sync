#include "repo/DirtyPathRepo.hpp"

#include "db/SqliteHelpers.hpp"

#include <stdexcept>

namespace {

DirtyPath readDirtyPath(sqlite3_stmt *stmt) {
  const char *syncRootId =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
  const char *relativePath =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
  const char *eventType =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
  const char *createdAt =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));

  if (syncRootId == nullptr || relativePath == nullptr ||
      eventType == nullptr || createdAt == nullptr) {
    throw std::runtime_error("dirty_paths row contained NULL value");
  }

  return DirtyPath{
      .syncRootId = syncRootId,
      .relativePath = relativePath,
      .eventType = eventType,
      .createdAt = createdAt,
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
  sync_root_id,
  relative_path,
  event_type,
  created_at
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
