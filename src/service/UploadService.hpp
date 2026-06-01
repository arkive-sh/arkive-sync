#pragma once

#include "api/ArkiveHttpClient.hpp"
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

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

struct EncryptedUploadPart {
  int partNumber;
  std::vector<std::byte> body;
  std::string encryptedHash;
};

struct UploadFileRequest {
  StartUploadRequest start;
  std::vector<EncryptedUploadPart> parts;
  std::optional<PresignThumbnailRequest> thumbnail;
  std::vector<std::byte> encryptedThumbnailBody;
  UploadCompleteRequest complete;
};

struct UploadFileResponse {
  std::string fileId;
  std::string vaultId;
  std::string uploadSessionId;
  std::string providerUploadId;
};

class UploadService {
public:
  explicit UploadService(ArkiveHttpClient &client);

  UploadFileResponse uploadFile(const UploadFileRequest &request);

private:
  // HTTP: POST /api/uploads/start
  StartUploadResponse startUpload(const StartUploadRequest &request);

  // HTTP: POST /api/uploads/:id/parts/presign
  PresignPartsResponse presignParts(const std::string &uploadSessionId,
                                    const std::vector<int> &partNumbers);

  // HTTP: PUT <presigned storage URL>
  std::string putEncryptedPartToStorage(const std::string &presignedUrl,
                                        const std::vector<std::byte> &body);

  // HTTP: POST /api/uploads/:id/thumbnail/presign
  std::string presignThumbnail(const std::string &uploadSessionId,
                               const PresignThumbnailRequest &request);

  // HTTP: POST /api/uploads/:id/parts
  void uploadPart(const std::string &uploadSessionId,
                  const UploadPartRequest &request);

  // HTTP: POST /api/uploads/:id/complete
  void uploadComplete(const std::string &uploadSessionId,
                      const UploadCompleteRequest &request);

  ArkiveHttpClient &client_;
};
