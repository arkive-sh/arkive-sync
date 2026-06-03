#include "repo/QueueRepo.hpp"
#include "db/SqliteHelpers.hpp"

#include <sqlite3.h>
#include <stdexcept>

namespace {

TransferJob readTransferJob(sqlite3_stmt *stmt) {
  const char *id = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
  const char *entryId =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
  const char *direction =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
  const char *status =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
  const char *localPath =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
  const char *remoteId =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5));
  const char *remoteFolderId =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 6));

  if (id == nullptr || entryId == nullptr || direction == nullptr ||
      status == nullptr || localPath == nullptr) {
    throw std::invalid_argument("transfer_queue row contained NULL value");
  }

  return TransferJob{
      .id = id,
      .entryId = entryId,
      .direction = direction,
      .status = status,
      .localPath = localPath,
      .remoteId = remoteId != nullptr ? std::optional<std::string>(remoteId)
                                      : std::nullopt,
      .remoteFolderId = remoteFolderId != nullptr
                            ? std::optional<std::string>(remoteFolderId)
                            : std::nullopt,
      .bytesTotal = static_cast<uint64_t>(sqlite3_column_int64(stmt, 7)),
      .bytesDone = static_cast<uint64_t>(sqlite3_column_int64(stmt, 8)),
      .retryCount = sqlite3_column_int(stmt, 9),
  };
}

std::string generateQueueId() {
  sqlite3_int64 first = 0;
  sqlite3_int64 second = 0;
  sqlite3_randomness(sizeof(first), &first);
  sqlite3_randomness(sizeof(second), &second);
  return std::to_string(static_cast<long long>(first)) +
         std::to_string(static_cast<long long>(second));
}

} // namespace

QueueRepo::QueueRepo(sqlite3 *db) : db_(db) {
  if (db == nullptr) {
    throw std::invalid_argument("Queue Repo needs a valid sqlite3 connection");
  }
}

bool QueueRepo::hasActiveUploadForEntry(const std::string &entryId) {
  static constexpr const char *hasActiveUploadSql = R"sql(
SELECT 1
FROM transfer_queue
WHERE entry_id = ?
  AND direction = 'upload'
  AND status IN ('queued', 'running')
LIMIT 1;
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, hasActiveUploadSql, -1, &rawStmt, nullptr) !=
      SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, entryId);

  const int rc = sqlite3_step(stmt.get());
  if (rc == SQLITE_DONE) {
    return false;
  }
  if (rc != SQLITE_ROW) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }

  return true;
}

void QueueRepo::enqueueUpload(const std::string &entryId,
                              const std::string &localPath,
                              const std::optional<std::string> &remoteFolderId,
                              uint64_t bytesTotal) {
  static constexpr const char *enqueueUploadSql = R"sql(
INSERT OR IGNORE INTO transfer_queue (
  id,
  entry_id,
  direction,
  status,
  local_path,
  remote_id,
  folder_id,
  bytes_total,
  bytes_done,
  error_message,
  retry_count,
  created_at,
  updated_at
) VALUES (
  ?,
  ?,
  'upload',
  'queued',
  ?,
  NULL,
  ?,
  ?,
  0,
  NULL,
  0,
  CURRENT_TIMESTAMP,
  CURRENT_TIMESTAMP
);
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, enqueueUploadSql, -1, &rawStmt, nullptr) !=
      SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, generateQueueId());
  bindText(db_, stmt.get(), 2, entryId);
  bindText(db_, stmt.get(), 3, localPath);
  bindOptionalText(db_, stmt.get(), 4, remoteFolderId);
  throwIfBindFailed(db_,
                    sqlite3_bind_int64(stmt.get(), 5,
                                       static_cast<sqlite3_int64>(bytesTotal)));

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }
}

QueueStats QueueRepo::stats() {
  static constexpr const char *statsSql = R"sql(
SELECT
  SUM(CASE WHEN status = 'queued' THEN 1 ELSE 0 END),
  SUM(CASE WHEN status = 'running' THEN 1 ELSE 0 END),
  SUM(CASE WHEN status = 'failed' THEN 1 ELSE 0 END),
  SUM(CASE WHEN status = 'done' THEN 1 ELSE 0 END)
FROM transfer_queue;
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, statsSql, -1, &rawStmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_ROW) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }

  return QueueStats{
      .queued = sqlite3_column_int(stmt.get(), 0),
      .running = sqlite3_column_int(stmt.get(), 1),
      .failed = sqlite3_column_int(stmt.get(), 2),
      .done = sqlite3_column_int(stmt.get(), 3),
  };
}

std::optional<TransferJob> QueueRepo::claimNextQueuedUpload() {
  static constexpr const char *claimNextQueuedUploadSql = R"sql(
SELECT
  id,
  entry_id,
  direction,
  status,
  local_path,
  remote_id,
  folder_id,
  bytes_total,
  bytes_done,
  retry_count
FROM transfer_queue
WHERE direction = 'upload'
  AND status = 'queued'
ORDER BY created_at ASC
LIMIT 1;
  )sql";

  static constexpr const char *markRunningSql = R"sql(
UPDATE transfer_queue
SET
  status = 'running',
  updated_at = CURRENT_TIMESTAMP
WHERE id = ?
AND status = 'queued';
  )sql";

  execOrThrow(db_, "BEGIN IMMEDIATE;");

  try {
    sqlite3_stmt *rawSelectStmt = nullptr;
    if (sqlite3_prepare_v2(db_, claimNextQueuedUploadSql, -1, &rawSelectStmt,
                           nullptr) != SQLITE_OK) {
      throw std::runtime_error(std::string("Prepare failed: ") +
                               sqlite3_errmsg(db_));
    }

    StmtUniquePtr selectStmt(rawSelectStmt);
    const int selectRc = sqlite3_step(selectStmt.get());
    if (selectRc == SQLITE_DONE) {
      execOrThrow(db_, "COMMIT;");
      return std::nullopt;
    }
    if (selectRc != SQLITE_ROW) {
      throw std::runtime_error(std::string("Step failed: ") +
                               sqlite3_errmsg(db_));
    }

    TransferJob job = readTransferJob(selectStmt.get());

    sqlite3_stmt *rawUpdateStmt = nullptr;
    if (sqlite3_prepare_v2(db_, markRunningSql, -1, &rawUpdateStmt, nullptr) !=
        SQLITE_OK) {
      throw std::runtime_error(std::string("Prepare failed: ") +
                               sqlite3_errmsg(db_));
    }

    StmtUniquePtr updateStmt(rawUpdateStmt);
    bindText(db_, updateStmt.get(), 1, job.id);

    const int updateRc = sqlite3_step(updateStmt.get());
    if (updateRc != SQLITE_DONE) {
      throw std::runtime_error(std::string("Step failed: ") +
                               sqlite3_errmsg(db_));
    }

    if (sqlite3_changes(db_) != 1) {
      throw std::runtime_error("Failed to modify");
    }

    execOrThrow(db_, "COMMIT;");
    job.status = "running";
    return job;
  } catch (...) {
    sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
    throw;
  }
}

void QueueRepo::markDone(const std::string &jobId) {
  static constexpr const char *markDoneSql = R"sql(
UPDATE transfer_queue
SET
  status = 'done',
  bytes_done = bytes_total,
  error_message = NULL,
  updated_at = CURRENT_TIMESTAMP
WHERE id = ?;
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, markDoneSql, -1, &rawStmt, nullptr) !=
      SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, jobId);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }
}

void QueueRepo::markFailed(const std::string &jobId,
                           const std::string &errorMessage) {
  static constexpr const char *markFailedSql = R"sql(
UPDATE transfer_queue
SET
  status = 'failed',
  error_message = ?,
  updated_at = CURRENT_TIMESTAMP
WHERE id = ?;
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, markFailedSql, -1, &rawStmt, nullptr) !=
      SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, errorMessage);
  bindText(db_, stmt.get(), 2, jobId);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }
}

void QueueRepo::retryJob(const std::string &jobId) {
  static constexpr const char *retryJobSql = R"sql(
UPDATE transfer_queue
SET
  status = 'queued',
  bytes_done = 0,
  error_message = NULL,
  retry_count = retry_count + 1,
  updated_at = CURRENT_TIMESTAMP
WHERE id = ?;
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, retryJobSql, -1, &rawStmt, nullptr) !=
      SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, jobId);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }
}

void QueueRepo::incrementProgress(const std::string &jobId,
                                  uint64_t bytesDone) {
  static constexpr const char *incrementProgressSql = R"sql(
UPDATE transfer_queue
SET
  bytes_done = ?,
  updated_at = CURRENT_TIMESTAMP
WHERE id = ?;
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, incrementProgressSql, -1, &rawStmt, nullptr) !=
      SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  throwIfBindFailed(
      db_,
      sqlite3_bind_int64(stmt.get(), 1, static_cast<sqlite3_int64>(bytesDone)));
  bindText(db_, stmt.get(), 2, jobId);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }
}

void QueueRepo::retryFailed() {
  static constexpr const char *retryFailedSql = R"sql(
UPDATE transfer_queue
SET
  status = 'queued',
  error_message = NULL,
  retry_count = retry_count + 1,
  updated_at = CURRENT_TIMESTAMP
WHERE status = 'failed';
  )sql";

  execOrThrow(db_, retryFailedSql);
}

void QueueRepo::clearDone() {
  static constexpr const char *clearDoneSql = R"sql(
DELETE FROM transfer_queue
WHERE status = 'done';
  )sql";

  execOrThrow(db_, clearDoneSql);
}
