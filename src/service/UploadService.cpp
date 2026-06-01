#include "service/UploadService.hpp"
#include <algorithm>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace {

StartUploadResponse decodeStartUploadResponse(const nlohmann::json &json) {
  return StartUploadResponse{
      .fileId = json.value("fileId", ""),
      .vaultId = json.value("vaultId", ""),
      .uploadSessionId = json.value("uploadSessionId", ""),
      .providerUploadId = json.value("providerUploadId", ""),
      .fileChunkSize = json.value("fileChunkSize", int64_t{0}),
      .totalChunks = json.value("totalChunks", 0),
      .uploadPartSize = json.value("uploadPartSize", int64_t{0}),
      .uploadPartCount = json.value("uploadPartCount", 0),
  };
}

PresignPartsResponse decodePresignPartsResponse(const nlohmann::json &json) {
  PresignPartsResponse response;
  if (!json.contains("urls") || !json["urls"].is_object()) {
    return response;
  }

  for (const auto &[key, value] : json["urls"].items()) {
    response.urls.emplace(std::stoi(key), value.get<std::string>());
  }

  return response;
}

nlohmann::json encodeSearchTokens(
    const std::vector<UploadCompleteSearchToken> &searchTokens) {
  nlohmann::json payload = nlohmann::json::array();
  for (const auto &token : searchTokens) {
    payload.push_back({
        {"token", token.token},
        {"field", token.field},
        {"weight", token.weight},
    });
  }

  return payload;
}

} // namespace

UploadService::UploadService(ArkiveHttpClient &client) : client_(client) {}

UploadFileResponse UploadService::uploadFile(const UploadFileRequest &request) {
  if (request.parts.empty()) {
    throw std::invalid_argument("uploadFile requires at least one part");
  }

  const StartUploadResponse started = startUpload(request.start);

  std::vector<int> partNumbers;
  partNumbers.reserve(request.parts.size());
  for (const auto &part : request.parts) {
    partNumbers.push_back(part.partNumber);
  }

  const PresignPartsResponse presigned =
      presignParts(started.uploadSessionId, partNumbers);

  std::vector<EncryptedUploadPart> orderedParts = request.parts;
  std::sort(orderedParts.begin(), orderedParts.end(),
            [](const EncryptedUploadPart &left, const EncryptedUploadPart &right) {
              return left.partNumber < right.partNumber;
            });

  for (const auto &part : orderedParts) {
    const auto urlIt = presigned.urls.find(part.partNumber);
    if (urlIt == presigned.urls.end()) {
      throw std::runtime_error("missing presigned URL for upload part " +
                               std::to_string(part.partNumber));
    }

    const std::string etag = putEncryptedPartToStorage(urlIt->second, part.body);
    uploadPart(started.uploadSessionId,
               UploadPartRequest{
                   .partNumber = part.partNumber,
                   .encryptedHash = part.encryptedHash,
                   .etag = etag,
               });
  }

  if (request.thumbnail.has_value()) {
    if (request.encryptedThumbnailBody.empty()) {
      throw std::invalid_argument(
          "uploadFile thumbnail metadata requires thumbnail bytes");
    }

    const std::string thumbnailUrl =
        presignThumbnail(started.uploadSessionId, *request.thumbnail);
    putEncryptedPartToStorage(thumbnailUrl, request.encryptedThumbnailBody);
  } else if (!request.encryptedThumbnailBody.empty()) {
    throw std::invalid_argument(
        "uploadFile received thumbnail bytes without thumbnail metadata");
  }

  uploadComplete(started.uploadSessionId, request.complete);

  return UploadFileResponse{
      .fileId = started.fileId,
      .vaultId = started.vaultId,
      .uploadSessionId = started.uploadSessionId,
      .providerUploadId = started.providerUploadId,
  };
}

StartUploadResponse UploadService::startUpload(
    const StartUploadRequest &request) {
  nlohmann::json payload{
      {"originalSize", request.originalSize},
      {"fileChunkSize", request.fileChunkSize},
      {"totalChunks", request.totalChunks},
      {"uploadPartSize", request.uploadPartSize},
      {"uploadPartCount", request.uploadPartCount},
      {"encryptionVersion", request.encryptionVersion},
      {"folderId", request.folderId.has_value()
                       ? nlohmann::json(*request.folderId)
                       : nlohmann::json(nullptr)},
  };

  return decodeStartUploadResponse(
      client_.postJson("/api/uploads/start", payload));
}

PresignPartsResponse
UploadService::presignParts(const std::string &uploadSessionId,
                            const std::vector<int> &partNumbers) {
  return decodePresignPartsResponse(client_.postJson(
      "/api/uploads/" + uploadSessionId + "/parts/presign",
      {{"parts", partNumbers}}));
}

std::string
UploadService::putEncryptedPartToStorage(const std::string &presignedUrl,
                                         const std::vector<std::byte> &body) {
  return client_.putBytes(presignedUrl, body);
}

std::string UploadService::presignThumbnail(
    const std::string &uploadSessionId,
    const PresignThumbnailRequest &request) {
  const auto response = client_.postJson(
      "/api/uploads/" + uploadSessionId + "/thumbnail/presign",
      {
          {"encryptedSize", request.encryptedSize},
          {"mime", request.mime},
          {"width", request.width},
          {"height", request.height},
      });

  return response.value("url", "");
}

void UploadService::uploadPart(const std::string &uploadSessionId,
                               const UploadPartRequest &request) {
  client_.postJson("/api/uploads/" + uploadSessionId + "/parts",
                   {
                       {"partNumber", request.partNumber},
                       {"encryptedHash", request.encryptedHash},
                       {"etag", request.etag},
                   });
}

void UploadService::uploadComplete(const std::string &uploadSessionId,
                                   const UploadCompleteRequest &request) {
  client_.postJson(
      "/api/uploads/" + uploadSessionId + "/complete",
      {
          {"encryptedMetadata", request.encryptedMetadata},
          {"encryptedFileKey", request.encryptedFileKey},
          {"encryptedManifest", request.encryptedManifest},
          {"encryptedHash", request.encryptedHash},
          {"searchTokens", encodeSearchTokens(request.searchTokens)},
          {"hasThumbnail", request.hasThumbnail},
          {"thumbnailMime", request.thumbnailMime},
          {"thumbnailWidth", request.thumbnailWidth},
          {"thumbnailHeight", request.thumbnailHeight},
      });
}
