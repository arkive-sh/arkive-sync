#include <sqlite3.h>

#include <filesystem>

#pragma once

class Database {
  sqlite3 *db = nullptr;
  std::filesystem::path dbPath_;

  // Avoid copying
  Database(const Database &) = delete;
  Database &operator=(const Database &) = delete;

  // Avoid moving
  Database(Database &&) = delete;
  Database &operator=(Database &&) = delete;

public:
  Database();
  explicit Database(std::filesystem::path dbPath);
  ~Database();
  sqlite3 *getDb();

private:
  void initDb();
  void migrateSchema();
  void verifySchema();
  void close();
};
