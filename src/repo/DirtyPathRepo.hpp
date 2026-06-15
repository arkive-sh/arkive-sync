#pragma once

#include "fs/FileWatcher.hpp"
#include <optional>
#include <sqlite3.h>
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

  void insertFullRescan(const std::string &syncRootId);
  void insertDirtyPath(const std::string &syncRootId,
                       const std::optional<std::string> &relativePath,
                       DirtyPathEventType action);
  void record(const FileEvent &event);
  std::vector<DirtyPath>
  getDirtyPathsBySyncRootId(const std::string &syncRootId);

private:
  sqlite3 *db_;
};
