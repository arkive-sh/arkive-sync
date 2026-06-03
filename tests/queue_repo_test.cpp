#include "db/SqliteHelpers.hpp"
#include "repo/QueueRepo.hpp"

#include <catch2/catch_test_macros.hpp>
#include <sqlite3.h>

namespace {

class TestDb {
public:
  TestDb() {
    REQUIRE(sqlite3_open(":memory:", &db_) == SQLITE_OK);
    execOrThrow(db_, R"sql(
CREATE TABLE transfer_queue (
  id TEXT PRIMARY KEY,
  entry_id TEXT,
  direction TEXT NOT NULL,
  status TEXT NOT NULL,
  local_path TEXT NOT NULL,
  remote_id TEXT,
  folder_id TEXT,
  bytes_total INTEGER,
  bytes_done INTEGER NOT NULL DEFAULT 0,
  error_message TEXT,
  retry_count INTEGER NOT NULL DEFAULT 0,
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE UNIQUE INDEX idx_transfer_active_upload
ON transfer_queue(entry_id, direction)
WHERE direction = 'upload'
AND status IN ('queued', 'running');
)sql");
  }

  ~TestDb() {
    if (db_ != nullptr) {
      sqlite3_close(db_);
    }
  }

  sqlite3 *get() const { return db_; }

private:
  sqlite3 *db_ = nullptr;
};

int countRows(sqlite3 *db, const char *sql) {
  sqlite3_stmt *rawStmt = nullptr;
  REQUIRE(sqlite3_prepare_v2(db, sql, -1, &rawStmt, nullptr) == SQLITE_OK);
  StmtUniquePtr stmt(rawStmt);
  REQUIRE(sqlite3_step(stmt.get()) == SQLITE_ROW);
  return sqlite3_column_int(stmt.get(), 0);
}

std::string readStatus(sqlite3 *db, const std::string &jobId) {
  sqlite3_stmt *rawStmt = nullptr;
  REQUIRE(sqlite3_prepare_v2(db,
                             "SELECT status FROM transfer_queue WHERE id = ?;",
                             -1, &rawStmt, nullptr) == SQLITE_OK);
  StmtUniquePtr stmt(rawStmt);
  bindText(db, stmt.get(), 1, jobId);
  REQUIRE(sqlite3_step(stmt.get()) == SQLITE_ROW);
  return reinterpret_cast<const char *>(sqlite3_column_text(stmt.get(), 0));
}

int readRetryCount(sqlite3 *db, const std::string &jobId) {
  sqlite3_stmt *rawStmt = nullptr;
  REQUIRE(sqlite3_prepare_v2(
              db, "SELECT retry_count FROM transfer_queue WHERE id = ?;", -1,
              &rawStmt, nullptr) == SQLITE_OK);
  StmtUniquePtr stmt(rawStmt);
  bindText(db, stmt.get(), 1, jobId);
  REQUIRE(sqlite3_step(stmt.get()) == SQLITE_ROW);
  return sqlite3_column_int(stmt.get(), 0);
}

} // namespace

TEST_CASE("QueueRepo enqueues upload once") {
  TestDb db;
  QueueRepo repo(db.get());

  repo.enqueueUpload("entry-1", "", std::nullopt, 123);

  REQUIRE(countRows(db.get(), "SELECT COUNT(*) FROM transfer_queue;") == 1);
  REQUIRE(repo.hasActiveUploadForEntry("entry-1"));
}

TEST_CASE("QueueRepo prevents duplicate active upload") {
  TestDb db;
  QueueRepo repo(db.get());

  repo.enqueueUpload("entry-1", "", std::nullopt, 123);
  repo.enqueueUpload("entry-1", "", std::nullopt, 123);

  REQUIRE(countRows(db.get(), "SELECT COUNT(*) FROM transfer_queue;") == 1);
}

TEST_CASE("QueueRepo claims queued upload and marks it running") {
  TestDb db;
  QueueRepo repo(db.get());

  repo.enqueueUpload("entry-1", "", std::optional<std::string>("folder-1"),
                     123);

  const std::optional<TransferJob> claimed = repo.claimNextQueuedUpload();

  REQUIRE(claimed.has_value());
  REQUIRE(claimed->entryId == "entry-1");
  REQUIRE(claimed->status == "running");
  REQUIRE(claimed->remoteFolderId == std::optional<std::string>("folder-1"));
  REQUIRE(readStatus(db.get(), claimed->id) == "running");
}

TEST_CASE("QueueRepo retries failed uploads") {
  TestDb db;
  QueueRepo repo(db.get());

  repo.enqueueUpload("entry-1", "", std::nullopt, 123);
  const std::optional<TransferJob> claimed = repo.claimNextQueuedUpload();
  REQUIRE(claimed.has_value());

  repo.markFailed(claimed->id, "boom");
  repo.retryFailed();

  REQUIRE(readStatus(db.get(), claimed->id) == "queued");
  REQUIRE(readRetryCount(db.get(), claimed->id) == 1);
}

TEST_CASE("QueueRepo clears done uploads") {
  TestDb db;
  QueueRepo repo(db.get());

  repo.enqueueUpload("entry-1", "", std::nullopt, 123);
  const std::optional<TransferJob> claimed = repo.claimNextQueuedUpload();
  REQUIRE(claimed.has_value());
  repo.markDone(claimed->id);

  REQUIRE(
      countRows(db.get(),
                "SELECT COUNT(*) FROM transfer_queue WHERE status = 'done';") ==
      1);

  repo.clearDone();

  REQUIRE(countRows(db.get(), "SELECT COUNT(*) FROM transfer_queue;") == 0);
}

TEST_CASE("QueueRepo returns no job when queue is empty") {
  TestDb db;
  QueueRepo repo(db.get());

  const auto claimed = repo.claimNextQueuedUpload();

  REQUIRE_FALSE(claimed.has_value());
}
