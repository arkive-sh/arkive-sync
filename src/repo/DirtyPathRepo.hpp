#pragma once

#include <sqlite3.h>
#include <optional>
#include <string>
#include <vector>

enum class DirtyPathEventType {
  Scan,
  Delete,
  FullRescan,
};

enum class DirtyPathStatus {
  Pending,
  Running,
  Done,
  Failed,
};

struct DirtyPath {
  std::string id;
  std::string syncRootId;
  std::optional<std::string> relativePath;
  DirtyPathEventType eventType;
  DirtyPathStatus status;
  std::string createdAt;
  std::string updatedAt;
};

class DirtyPathRepo {
public:
  explicit DirtyPathRepo(sqlite3 *db);

  std::vector<DirtyPath>
  getDirtyPathsBySyncRootId(const std::string &syncRootId);

private:
  sqlite3 *db_;
};
