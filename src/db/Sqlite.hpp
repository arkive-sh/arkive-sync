#include <sqlite3.h>

#pragma once

class Database {
  sqlite3 *db = nullptr;

  // Avoid copying
  Database(const Database &) = delete;
  Database &operator=(const Database &) = delete;

  // Avoid moving
  Database(Database &&) = delete;
  Database &operator=(Database &&) = delete;

public:
  Database();
  ~Database();
  sqlite3 *getDb();

private:
  void initDb();
  void migrateSchema();
  void verifySchema();
  void close();
};
