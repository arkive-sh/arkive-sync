#include "db/SqliteHelpers.hpp"

#include <stdexcept>

void SQLiteStmtDeleter::operator()(sqlite3_stmt *stmt) const {
  if (stmt != nullptr) {
    sqlite3_finalize(stmt);
  }
}

void throwIfBindFailed(sqlite3 *db, int rc) {
  if (rc != SQLITE_OK) {
    throw std::runtime_error(std::string("Bind failed: ") + sqlite3_errmsg(db));
  }
}

void bindText(sqlite3 *db, sqlite3_stmt *stmt, int index,
              const std::string &value) {
  throwIfBindFailed(
      db, sqlite3_bind_text(stmt, index, value.c_str(), -1, SQLITE_TRANSIENT));
}

void bindOptionalText(sqlite3 *db, sqlite3_stmt *stmt, int index,
                      const std::optional<std::string> &value) {
  if (value.has_value()) {
    bindText(db, stmt, index, *value);
    return;
  }

  throwIfBindFailed(db, sqlite3_bind_null(stmt, index));
}
