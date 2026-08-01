#pragma once

#include "api/ArkiveHttpClient.hpp"
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

struct LoginResponse {
  std::string salt;
  std::string encryptedMasterKey;
};

struct UploadLimitsResponse {
  int maxQueueItems;
  int partConcurrency;
  int staleUploadHours;
};

struct StartUploadRequest {
  int64_t originalSize;
  int64_t fileChunkSize;
  int totalChunks;
  int64_t uploadPartSize;
  int uploadPartCount;
  int16_t encryptionVersion;
  std::optional<std::string> folderId;
};

struct StartUploadResponse {
  std::string fileId;
  std::string vaultId;
  std::string uploadSessionId;
  std::string providerUploadId;
  int64_t fileChunkSize;
  int totalChunks;
  int64_t uploadPartSize;
  int uploadPartCount;
};

struct PresignPartsResponse {
  std::map<int, std::string> urls;
};

struct PresignThumbnailRequest {
  int64_t encryptedSize;
  std::string mime;
  int width;
  int height;
};

struct UploadPartRequest {
  int partNumber;
  std::string encryptedHash;
  std::string etag;
};

struct UploadCompleteSearchToken {
  std::string token;
  std::string field;
  int weight;
};

struct CreateFolderRequest {
  std::optional<std::string> parentFolderId;
  std::string encryptedName;
  std::string encryptedMetadata;
  std::vector<UploadCompleteSearchToken> searchTokens;
};

struct CreateFolderResponse {
  std::string id;
  std::optional<std::string> parentFolderId;
  std::string encryptedName;
  std::optional<std::string> encryptedMetadata;
};

struct UploadCompleteRequest {
  std::string encryptedMetadata;
  std::string encryptedFileKey;
  std::string encryptedManifest;
  std::string encryptedHash;
  std::vector<UploadCompleteSearchToken> searchTokens;
  bool hasThumbnail;
  std::string thumbnailMime;
  int thumbnailWidth;
  int thumbnailHeight;
};

struct ListSyncEntriesRequest {
  std::optional<std::string> folderId;
  bool includeDeleted{false};
  size_t limit{100};
  std::optional<std::string> cursor;
};

struct SyncEntryResponse {
  std::string remoteId;
  std::string type;
  std::optional<std::string> remoteFileId;
  std::optional<std::string> remoteFolderId;
  std::optional<std::string> remoteParentFolderId;
  std::optional<std::string> encryptedName;
  std::optional<std::string> encryptedMetadata;
  std::optional<std::string> encryptedFileKey;
  std::optional<std::string> encryptedManifest;
  std::optional<std::string> deletedAt;
  std::string updatedAt;
};

struct ListSyncEntriesResponse {
  std::vector<SyncEntryResponse> entries;
  std::optional<std::string> nextCursor;
  bool hasMore{false};
};

struct FileRecordResponse {
  std::string fileId;
  std::string vaultId;
  int16_t encryptionVersion{0};
  int64_t chunkSize{0};
  int totalChunks{0};
  int64_t plaintextSize{0};
  std::string encryptedHash;
  std::string encryptedMetadata;
  std::string encryptedFileKey;
  std::string encryptedManifest;
  std::string sourceUrl;
};

class ArkiveApi {
public:
  explicit ArkiveApi(ArkiveHttpClient &client);
  virtual ~ArkiveApi() = default;

  virtual LoginResponse login(const std::string &email,
                              const std::string &password);
  virtual LoginResponse unlockVault(const std::string &password);
  virtual void logout();
  virtual nlohmann::json me();

  virtual UploadLimitsResponse uploadLimits();
  virtual CreateFolderResponse createFolder(const CreateFolderRequest &request);
  virtual StartUploadResponse startUpload(const StartUploadRequest &request);
  virtual PresignPartsResponse
  presignParts(const std::string &uploadSessionId,
               const std::vector<int> &partNumbers);
  virtual std::string
  putEncryptedPartToStorage(const std::string &presignedUrl,
                            const std::vector<uint8_t> &body);
  virtual std::string presignThumbnail(const std::string &uploadSessionId,
                                       const PresignThumbnailRequest &request);
  virtual void uploadPart(const std::string &uploadSessionId,
                          const UploadPartRequest &request);
  virtual void uploadComplete(const std::string &uploadSessionId,
                              const UploadCompleteRequest &request);
  virtual ListSyncEntriesResponse
  listSyncEntries(const std::optional<std::string> &folderId,
                  bool includeDeleted);
  virtual ListSyncEntriesResponse
  listSyncEntries(const ListSyncEntriesRequest &request);
  virtual FileRecordResponse getFileRecord(const std::string &fileId);

private:
  ArkiveHttpClient &client_;
};
