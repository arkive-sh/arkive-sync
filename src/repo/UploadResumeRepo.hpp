#pragma once

#include <optional>
#include <sqlite3.h>
#include <string>
#include <vector>

struct UploadResumeSessionRecord {
  std::string id;
  std::optional<std::string> entryId;
  std::string localPath;
  int64_t localSize = 0;
  std::optional<std::string> localMtime;
  std::optional<std::string> localHash;
  std::optional<std::string> folderId;
  std::string vaultId;
  std::string fileId;
  std::string uploadSessionId;
  std::string providerUploadId;
  int64_t fileChunkSize = 0;
  int totalChunks = 0;
  int64_t uploadPartSize = 0;
  int uploadPartCount = 0;
  std::string encryptedFileKeyBlob;
};

struct UploadResumePartRecord {
  int partNumber = 0;
  std::string etag;
  std::string uploadHash;
  std::string chunkManifestJson;
  std::string combinedChunkHashes;
};

class UploadResumeRepo {
public:
  explicit UploadResumeRepo(sqlite3 *db);

  std::optional<UploadResumeSessionRecord>
  getSessionByLocalPath(const std::string &localPath) const;
  std::optional<UploadResumeSessionRecord>
  getSessionByUploadSessionId(const std::string &uploadSessionId) const;

  void replaceSession(const UploadResumeSessionRecord &session) const;
  std::vector<UploadResumePartRecord>
  listParts(const std::string &uploadSessionId) const;
  void upsertPart(const std::string &uploadSessionId,
                  const UploadResumePartRecord &part) const;
  void deleteSessionByLocalPath(const std::string &localPath) const;
  void deleteSessionByUploadSessionId(const std::string &uploadSessionId) const;

private:
  void deletePartsByUploadSessionId(const std::string &uploadSessionId) const;

  sqlite3 *db_;
};
