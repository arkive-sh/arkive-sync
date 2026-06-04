#include "repo/UploadResumeRepo.hpp"

#include "db/SqliteHelpers.hpp"

#include <sqlite3.h>
#include <stdexcept>

namespace {

UploadResumeSessionRecord readSessionRecord(sqlite3_stmt *stmt) {
  const char *id = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
  const char *entryId =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
  const char *localPath =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
  const char *localMtime =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
  const char *localHash =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5));
  const char *folderId =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 6));
  const char *vaultId =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 7));
  const char *fileId =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 8));
  const char *uploadSessionId =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 9));
  const char *providerUploadId =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 10));
  const char *encryptedFileKeyBlob =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 15));

  if (id == nullptr || localPath == nullptr || vaultId == nullptr ||
      fileId == nullptr || uploadSessionId == nullptr ||
      providerUploadId == nullptr || encryptedFileKeyBlob == nullptr) {
    throw std::invalid_argument(
        "upload_resume_sessions row contained NULL value");
  }

  return UploadResumeSessionRecord{
      .id = id,
      .entryId = entryId != nullptr ? std::optional<std::string>(entryId)
                                    : std::nullopt,
      .localPath = localPath,
      .localSize = sqlite3_column_int64(stmt, 3),
      .localMtime = localMtime != nullptr ? std::optional<std::string>(localMtime)
                                          : std::nullopt,
      .localHash = localHash != nullptr ? std::optional<std::string>(localHash)
                                        : std::nullopt,
      .folderId = folderId != nullptr ? std::optional<std::string>(folderId)
                                      : std::nullopt,
      .vaultId = vaultId,
      .fileId = fileId,
      .uploadSessionId = uploadSessionId,
      .providerUploadId = providerUploadId,
      .fileChunkSize = sqlite3_column_int64(stmt, 11),
      .totalChunks = sqlite3_column_int(stmt, 12),
      .uploadPartSize = sqlite3_column_int64(stmt, 13),
      .uploadPartCount = sqlite3_column_int(stmt, 14),
      .encryptedFileKeyBlob = encryptedFileKeyBlob,
  };
}

UploadResumePartRecord readPartRecord(sqlite3_stmt *stmt) {
  const char *etag = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
  const char *uploadHash =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
  const char *chunkManifestJson =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
  const char *combinedChunkHashes =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));

  if (etag == nullptr || uploadHash == nullptr || chunkManifestJson == nullptr ||
      combinedChunkHashes == nullptr) {
    throw std::invalid_argument("upload_resume_parts row contained NULL value");
  }

  return UploadResumePartRecord{
      .partNumber = sqlite3_column_int(stmt, 0),
      .etag = etag,
      .uploadHash = uploadHash,
      .chunkManifestJson = chunkManifestJson,
      .combinedChunkHashes = combinedChunkHashes,
  };
}

} // namespace

UploadResumeRepo::UploadResumeRepo(sqlite3 *db) : db_(db) {
  if (db == nullptr) {
    throw std::invalid_argument(
        "UploadResumeRepo needs a valid sqlite3 connection");
  }
}

std::optional<UploadResumeSessionRecord>
UploadResumeRepo::getSessionByLocalPath(const std::string &localPath) const {
  static constexpr const char *sql = R"sql(
SELECT
  id,
  entry_id,
  local_path,
  local_size,
  local_mtime,
  local_hash,
  folder_id,
  vault_id,
  file_id,
  upload_session_id,
  provider_upload_id,
  file_chunk_size,
  total_chunks,
  upload_part_size,
  upload_part_count,
  encrypted_file_key_blob
FROM upload_resume_sessions
WHERE local_path = ?;
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, localPath);

  const int rc = sqlite3_step(stmt.get());
  if (rc == SQLITE_DONE) {
    return std::nullopt;
  }
  if (rc != SQLITE_ROW) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }

  return readSessionRecord(stmt.get());
}

std::optional<UploadResumeSessionRecord>
UploadResumeRepo::getSessionByUploadSessionId(
    const std::string &uploadSessionId) const {
  static constexpr const char *sql = R"sql(
SELECT
  id,
  entry_id,
  local_path,
  local_size,
  local_mtime,
  local_hash,
  folder_id,
  vault_id,
  file_id,
  upload_session_id,
  provider_upload_id,
  file_chunk_size,
  total_chunks,
  upload_part_size,
  upload_part_count,
  encrypted_file_key_blob
FROM upload_resume_sessions
WHERE upload_session_id = ?;
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, uploadSessionId);

  const int rc = sqlite3_step(stmt.get());
  if (rc == SQLITE_DONE) {
    return std::nullopt;
  }
  if (rc != SQLITE_ROW) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }

  return readSessionRecord(stmt.get());
}

void UploadResumeRepo::replaceSession(
    const UploadResumeSessionRecord &session) const {
  const auto existing = getSessionByLocalPath(session.localPath);
  if (existing.has_value()) {
    deleteSessionByUploadSessionId(existing->uploadSessionId);
  }

  static constexpr const char *sql = R"sql(
INSERT INTO upload_resume_sessions (
  id,
  entry_id,
  local_path,
  local_size,
  local_mtime,
  local_hash,
  folder_id,
  vault_id,
  file_id,
  upload_session_id,
  provider_upload_id,
  file_chunk_size,
  total_chunks,
  upload_part_size,
  upload_part_count,
  encrypted_file_key_blob,
  created_at,
  updated_at
) VALUES (
  ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP
);
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, session.id);
  bindOptionalText(db_, stmt.get(), 2, session.entryId);
  bindText(db_, stmt.get(), 3, session.localPath);
  throwIfBindFailed(db_, sqlite3_bind_int64(stmt.get(), 4, session.localSize));
  bindOptionalText(db_, stmt.get(), 5, session.localMtime);
  bindOptionalText(db_, stmt.get(), 6, session.localHash);
  bindOptionalText(db_, stmt.get(), 7, session.folderId);
  bindText(db_, stmt.get(), 8, session.vaultId);
  bindText(db_, stmt.get(), 9, session.fileId);
  bindText(db_, stmt.get(), 10, session.uploadSessionId);
  bindText(db_, stmt.get(), 11, session.providerUploadId);
  throwIfBindFailed(db_,
                    sqlite3_bind_int64(stmt.get(), 12, session.fileChunkSize));
  throwIfBindFailed(db_,
                    sqlite3_bind_int(stmt.get(), 13, session.totalChunks));
  throwIfBindFailed(db_,
                    sqlite3_bind_int64(stmt.get(), 14, session.uploadPartSize));
  throwIfBindFailed(db_,
                    sqlite3_bind_int(stmt.get(), 15, session.uploadPartCount));
  bindText(db_, stmt.get(), 16, session.encryptedFileKeyBlob);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }
}

std::vector<UploadResumePartRecord>
UploadResumeRepo::listParts(const std::string &uploadSessionId) const {
  static constexpr const char *sql = R"sql(
SELECT
  part_number,
  etag,
  upload_hash,
  chunk_manifest_json,
  combined_chunk_hashes
FROM upload_resume_parts
WHERE upload_session_id = ?
ORDER BY part_number ASC;
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, uploadSessionId);

  std::vector<UploadResumePartRecord> parts;
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) {
      break;
    }
    if (rc != SQLITE_ROW) {
      throw std::runtime_error(std::string("Step failed: ") +
                               sqlite3_errmsg(db_));
    }
    parts.push_back(readPartRecord(stmt.get()));
  }

  return parts;
}

void UploadResumeRepo::upsertPart(const std::string &uploadSessionId,
                                  const UploadResumePartRecord &part) const {
  static constexpr const char *sql = R"sql(
INSERT INTO upload_resume_parts (
  upload_session_id,
  part_number,
  etag,
  upload_hash,
  chunk_manifest_json,
  combined_chunk_hashes,
  created_at,
  updated_at
) VALUES (
  ?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP
)
ON CONFLICT (upload_session_id, part_number) DO UPDATE SET
  etag = excluded.etag,
  upload_hash = excluded.upload_hash,
  chunk_manifest_json = excluded.chunk_manifest_json,
  combined_chunk_hashes = excluded.combined_chunk_hashes,
  updated_at = CURRENT_TIMESTAMP;
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, uploadSessionId);
  throwIfBindFailed(db_, sqlite3_bind_int(stmt.get(), 2, part.partNumber));
  bindText(db_, stmt.get(), 3, part.etag);
  bindText(db_, stmt.get(), 4, part.uploadHash);
  bindText(db_, stmt.get(), 5, part.chunkManifestJson);
  bindText(db_, stmt.get(), 6, part.combinedChunkHashes);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }
}

void UploadResumeRepo::deletePartsByUploadSessionId(
    const std::string &uploadSessionId) const {
  static constexpr const char *sql = R"sql(
DELETE FROM upload_resume_parts
WHERE upload_session_id = ?;
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, uploadSessionId);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }
}

void UploadResumeRepo::deleteSessionByLocalPath(const std::string &localPath) const {
  const auto existing = getSessionByLocalPath(localPath);
  if (!existing.has_value()) {
    return;
  }
  deleteSessionByUploadSessionId(existing->uploadSessionId);
}

void UploadResumeRepo::deleteSessionByUploadSessionId(
    const std::string &uploadSessionId) const {
  deletePartsByUploadSessionId(uploadSessionId);

  static constexpr const char *sql = R"sql(
DELETE FROM upload_resume_sessions
WHERE upload_session_id = ?;
  )sql";

  sqlite3_stmt *rawStmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(rawStmt);
  bindText(db_, stmt.get(), 1, uploadSessionId);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }
}
