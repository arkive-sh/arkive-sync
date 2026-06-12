#pragma once

#include <sqlite3.h>
#include <string>

struct SyncRoot {
  std::string Id;
  std::string localPath;
  std::string localHash;
  std::string folderId;
  int enabled;
  std::string createdAt;
};

class SyncRepo {
public:
  explicit SyncRepo(sqlite3 *db);

  void upsertSyncRoot(const SyncRoot &input);

private:
  sqlite3 *db_;
};
