#pragma once

#include <optional>
#include <sqlite3.h>
#include <string>
#include <vector>

struct SyncRoot {
  std::string Id;
  std::string localPath;
  std::string folderId;
  int enabled;
  std::string createdAt;
};

class SyncRepo {
public:
  explicit SyncRepo(sqlite3 *db);

  std::vector<SyncRoot> getSyncRoots();
  void upsertSyncRoot(const SyncRoot &input);
  std::optional<SyncRoot> findSyncRootById(const std::string &syncRootId);

private:
  sqlite3 *db_;
};
