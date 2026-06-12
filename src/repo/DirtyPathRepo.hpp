#pragma once

#include <sqlite3.h>
#include <string>
#include <vector>

struct DirtyPath {
  std::string syncRootId;
  std::string relativePath;
  std::string eventType;
  std::string createdAt;
};

class DirtyPathRepo {
public:
  explicit DirtyPathRepo(sqlite3 *db);

  std::vector<DirtyPath>
  getDirtyPathsBySyncRootId(const std::string &syncRootId);

private:
  sqlite3 *db_;
};
