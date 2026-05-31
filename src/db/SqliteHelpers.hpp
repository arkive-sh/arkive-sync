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
void bindText(sqlite3 *db, sqlite3_stmt *stmt, int index,
              const std::string &value);
void bindOptionalText(sqlite3 *db, sqlite3_stmt *stmt, int index,
                      const std::optional<std::string> &value);
