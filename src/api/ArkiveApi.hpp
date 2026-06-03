#pragma once

#include "api/ArkiveHttpClient.hpp"
#include <cstddef>
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

class ArkiveApi {
public:
  explicit ArkiveApi(ArkiveHttpClient &client);

  LoginResponse login(const std::string &email, const std::string &password);
  LoginResponse unlockVault(const std::string &password);
  void logout();
  nlohmann::json me();

  UploadLimitsResponse uploadLimits();
  StartUploadResponse startUpload(const StartUploadRequest &request);
  PresignPartsResponse presignParts(const std::string &uploadSessionId,
                                    const std::vector<int> &partNumbers);
  std::string putEncryptedPartToStorage(const std::string &presignedUrl,
                                        const std::vector<std::byte> &body);
  std::string presignThumbnail(const std::string &uploadSessionId,
                               const PresignThumbnailRequest &request);
  void uploadPart(const std::string &uploadSessionId,
                  const UploadPartRequest &request);
  void uploadComplete(const std::string &uploadSessionId,
                      const UploadCompleteRequest &request);

private:
  ArkiveHttpClient &client_;
};
