#include "repo/ConflictRepo.hpp"

#include "db/SqliteHelpers.hpp"

#include <stdexcept>

ConflictRepo::ConflictRepo(sqlite3 *db) : db_(db) {
  if (db == nullptr) {
    throw std::invalid_argument("ConflictRepo needs valid sqlite3 connection");
  }
}

void ConflictRepo::markConflict(const std::string &entryId,
                                const std::string &state,
                                const std::string &reason) {
  static constexpr const char *sql = R"sql(
UPDATE entries
SET
  conflict_state = ?,
  conflict_reason = ?,
  updated_at = CURRENT_TIMESTAMP
WHERE id = ?;
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, state);
  bindText(db_, stmt.get(), 2, reason);
  bindText(db_, stmt.get(), 3, entryId);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }
}
