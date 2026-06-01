#include "api/ArkiveApi.hpp"
#include <nlohmann/json.hpp>

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

ArkiveApi::ArkiveApi(ArkiveHttpClient &client) : client_(client) {}

LoginResponse ArkiveApi::login(const std::string &email,
                               const std::string &password) {
  const auto responseJson = client_.postJson("/api/auth/login",
                                             {{"email", email},
                                              {"password", password}});
  return LoginResponse{
      .salt = responseJson.value("salt", ""),
      .encryptedMasterKey = responseJson.value("encryptedMasterKey", ""),
  };
}

void ArkiveApi::logout() { client_.postForm("/logout"); }

nlohmann::json ArkiveApi::me() { return client_.getJson("/api/me"); }

StartUploadResponse ArkiveApi::startUpload(const StartUploadRequest &request) {
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
ArkiveApi::presignParts(const std::string &uploadSessionId,
                        const std::vector<int> &partNumbers) {
  return decodePresignPartsResponse(client_.postJson(
      "/api/uploads/" + uploadSessionId + "/parts/presign",
      {{"parts", partNumbers}}));
}

std::string
ArkiveApi::putEncryptedPartToStorage(const std::string &presignedUrl,
                                     const std::vector<std::byte> &body) {
  return client_.putBytes(presignedUrl, body);
}

std::string ArkiveApi::presignThumbnail(
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

void ArkiveApi::uploadPart(const std::string &uploadSessionId,
                           const UploadPartRequest &request) {
  client_.postJson("/api/uploads/" + uploadSessionId + "/parts",
                   {
                       {"partNumber", request.partNumber},
                       {"encryptedHash", request.encryptedHash},
                       {"etag", request.etag},
                   });
}

void ArkiveApi::uploadComplete(const std::string &uploadSessionId,
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
