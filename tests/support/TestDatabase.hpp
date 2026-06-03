#pragma once

#include "db/Sqlite.hpp"

#include <sqlite3.h>

class TestDatabase {
public:
  TestDatabase() : db_(":memory:") {}

  sqlite3 *get() { return db_.getDb(); }

private:
  Database db_;
};
