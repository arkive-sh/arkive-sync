#pragma once

#include <memory>
#include <optional>
#include <sqlite3.h>
#include <string>

struct SQLiteStmtDeleter {
  void operator()(sqlite3_stmt *stmt) const;
};

using StmtUniquePtr = std::unique_ptr<sqlite3_stmt, SQLiteStmtDeleter>;

void throwIfBindFailed(sqlite3 *db, int rc);
void execOrThrow(sqlite3 *db, const char *sql);
void releaseMemory(sqlite3 *db);
void bindText(sqlite3 *db, sqlite3_stmt *stmt, int index,
              const std::string &value);
void bindOptionalText(sqlite3 *db, sqlite3_stmt *stmt, int index,
                      const std::optional<std::string> &value);
void bindOptionalInt64(sqlite3 *db, sqlite3_stmt *stmt, int index,
                       const std::optional<int64_t> &value);
