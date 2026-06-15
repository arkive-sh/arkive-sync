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

  bool insertScanJob(const ScanJob &scanJob);
  std::optional<ScanJob> getScanJob(const std::string &syncRootId);
  bool hasRunningScanJob(const std::string &syncRootId);
  void ensureRunningScanJob(const std::string &syncRootId);
  void updateScanCursor(const std::string &jobId, const std::string &cursorPath);
  void markScanComplete(const std::string &jobId);

private:
  sqlite3 *db_;
};
