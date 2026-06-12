#pragma once

#include <optional>
#include <sqlite3.h>
#include <string>

struct ScanJob {
  std::string id;
  std::string syncRootId;
  std::string status;
  std::optional<std::string> cursorPath;
  std::string startedAt;
  std::string updatedAt;
  std::optional<std::string> completedAt;
};

class ScanRepo {
public:
  explicit ScanRepo(sqlite3 *db);

  std::optional<ScanJob> getScanJob(const std::string &syncRootId);

private:
  sqlite3 *db_;
};
