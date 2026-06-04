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

void execOrThrow(sqlite3 *db, const char *sql) {
  char *errorMessage = nullptr;
  const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errorMessage);
  if (rc != SQLITE_OK) {
    const std::string message =
        errorMessage != nullptr ? errorMessage : sqlite3_errmsg(db);
    sqlite3_free(errorMessage);
    throw std::runtime_error(message);
  }
}

void releaseMemory(sqlite3 *db) {
  execOrThrow(db, "PRAGMA shrink_memory;");
  sqlite3_db_release_memory(db);
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

void bindOptionalInt64(sqlite3 *db, sqlite3_stmt *stmt, int index,
                       const std::optional<int64_t> &value) {
  if (value.has_value()) {
    throwIfBindFailed(db, sqlite3_bind_int64(stmt, index, *value));
    return;
  }

  throwIfBindFailed(db, sqlite3_bind_null(stmt, index));
}
