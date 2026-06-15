#include "api/ArkiveApi.hpp"
#include <nlohmann/json.hpp>
#include <sstream>

namespace {

UploadLimitsResponse decodeUploadLimitsResponse(const nlohmann::json &json) {
  return UploadLimitsResponse{
      .maxQueueItems = json.value("maxQueueItems", 0),
      .partConcurrency = json.value("partConcurrency", 0),
      .staleUploadHours = json.value("staleUploadHours", 0),
  };
}

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

std::optional<std::string> optionalString(const nlohmann::json &json,
                                          const char *key) {
  if (!json.contains(key) || json[key].is_null()) {
    return std::nullopt;
  }
  return json[key].get<std::string>();
}

CreateFolderResponse decodeCreateFolderResponse(const nlohmann::json &json) {
  const nlohmann::json folder =
      json.contains("folder") && json["folder"].is_object()
          ? json["folder"]
          : nlohmann::json::object();
  return CreateFolderResponse{
      .id = folder.value("id", ""),
      .parentFolderId = optionalString(folder, "parentFolderId"),
      .encryptedName = folder.value("encryptedName", ""),
      .encryptedMetadata = optionalString(folder, "encryptedMetadata"),
  };
}

SyncEntryResponse decodeSyncEntryResponse(const nlohmann::json &json) {
  return SyncEntryResponse{
      .type = json.value("type", ""),
      .id = json.value("id", ""),
      .folderId = optionalString(json, "folder_id"),
      .parentFolderId = optionalString(json, "parent_folder_id"),
      .encryptedMetadata = optionalString(json, "encrypted_metadata"),
      .encryptedFileKey = optionalString(json, "encrypted_file_key"),
      .encryptedManifest = optionalString(json, "encrypted_manifest"),
      .encryptedName = optionalString(json, "encrypted_name"),
      .updatedAt = json.value("updated_at", ""),
      .deletedAt = optionalString(json, "deleted_at"),
      .purgedAt = optionalString(json, "purged_at"),
  };
}

ListSyncEntriesResponse
decodeListSyncEntriesResponse(const nlohmann::json &json) {
  ListSyncEntriesResponse response;
  response.nextCursor = optionalString(json, "next_cursor");
  response.hasMore = json.value("has_more", false);
  if (!json.contains("entries") || !json["entries"].is_array()) {
    return response;
  }

  for (const auto &entryJson : json["entries"]) {
    if (!entryJson.is_object()) {
      continue;
    }
    response.entries.push_back(decodeSyncEntryResponse(entryJson));
  }

  return response;
}

std::string buildListSyncEntriesPath(const ListSyncEntriesRequest &request) {
  std::ostringstream path;
  path << "/api/sync/entries";

  bool hasQuery = false;
  if (request.folderId.has_value()) {
    path << (hasQuery ? "&" : "?") << "folder_id=" << *request.folderId;
    hasQuery = true;
  }
  if (request.includeDeleted) {
    path << (hasQuery ? "&" : "?") << "include_deleted=true";
    hasQuery = true;
  }
  if (request.limit > 0) {
    path << (hasQuery ? "&" : "?") << "limit=" << request.limit;
    hasQuery = true;
  }
  if (request.cursor.has_value()) {
    path << (hasQuery ? "&" : "?") << "cursor=" << *request.cursor;
  }

  return path.str();
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

LoginResponse ArkiveApi::unlockVault(const std::string &password) {
  const auto responseJson =
      client_.postJson("/api/auth/unlock", {{"password", password}});
  return LoginResponse{
      .salt = responseJson.value("salt", ""),
      .encryptedMasterKey = responseJson.value("encryptedMasterKey", ""),
  };
}

void ArkiveApi::logout() { client_.postForm("/logout"); }

nlohmann::json ArkiveApi::me() { return client_.getJson("/api/me"); }

UploadLimitsResponse ArkiveApi::uploadLimits() {
  return decodeUploadLimitsResponse(client_.getJson("/api/uploads/limits"));
}

CreateFolderResponse ArkiveApi::createFolder(const CreateFolderRequest &request) {
  return decodeCreateFolderResponse(client_.postJson(
      "/api/folders",
      {
          {"parentFolderId", request.parentFolderId.has_value()
                                 ? nlohmann::json(*request.parentFolderId)
                                 : nlohmann::json(nullptr)},
          {"encryptedName", request.encryptedName},
          {"encryptedMetadata", request.encryptedMetadata},
          {"searchTokens", encodeSearchTokens(request.searchTokens)},
      }));
}

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
                                     const std::vector<uint8_t> &body) {
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

ListSyncEntriesResponse
ArkiveApi::listSyncEntries(const ListSyncEntriesRequest &request) {
  return decodeListSyncEntriesResponse(
      client_.getJson(buildListSyncEntriesPath(request)));
}
