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
      .uploadUrl = json.value("uploadUrl", ""),
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
  const std::string type = json.value("type", "");
  const std::string remoteId = json.value("id", "");
  return SyncEntryResponse{
      .remoteId = remoteId,
      .type = type,
      .remoteFileId =
          type == "file" ? std::optional<std::string>(remoteId) : std::nullopt,
      .remoteFolderId = type == "folder"
                            ? std::optional<std::string>(remoteId)
                            : optionalString(json, "folder_id"),
      .remoteParentFolderId = optionalString(json, "parent_folder_id"),
      .encryptedName = optionalString(json, "encrypted_name"),
      .encryptedMetadata = optionalString(json, "encrypted_metadata"),
      .encryptedFileKey = optionalString(json, "encrypted_file_key"),
      .encryptedManifest = optionalString(json, "encrypted_manifest"),
      .deletedAt = optionalString(json, "deleted_at"),
      .updatedAt = json.value("updated_at", ""),
  };
}

FileRecordResponse decodeFileRecordResponse(const nlohmann::json &json) {
  return FileRecordResponse{
      .fileId = json.value("fileId", ""),
      .vaultId = json.value("vaultId", ""),
      .encryptionVersion = json.value("encryptionVersion", int16_t{0}),
      .chunkSize = json.value("chunkSize", int64_t{0}),
      .totalChunks = json.value("totalChunks", 0),
      .plaintextSize = json.value("plaintextSize", int64_t{0}),
      .encryptedHash = json.value("encryptedHash", ""),
      .encryptedMetadata = json.value("encryptedMetadata", ""),
      .encryptedFileKey = json.value("encryptedFileKey", ""),
      .encryptedManifest = json.value("encryptedManifest", ""),
      .sourceUrl = json.value("sourceUrl", ""),
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

nlohmann::json encodeStartUploadRequest(const StartUploadRequest &request) {
  return {
      {"originalSize", request.originalSize},
      {"fileChunkSize", request.fileChunkSize},
      {"totalChunks", request.totalChunks},
      {"uploadPartSize", request.uploadPartSize},
      {"uploadPartCount", request.uploadPartCount},
      {"encryptionVersion", request.encryptionVersion},
      {"folderId", request.folderId.has_value()
                       ? nlohmann::json(*request.folderId)
                       : nlohmann::json(nullptr)},
      {"singlePart", request.singlePart},
  };
}

nlohmann::json encodeUploadCompleteRequest(
    const UploadCompleteRequest &request) {
  nlohmann::json payload{
      {"encryptedMetadata", request.encryptedMetadata},
      {"encryptedFileKey", request.encryptedFileKey},
      {"encryptedManifest", request.encryptedManifest},
      {"encryptedHash", request.encryptedHash},
      {"searchTokens", encodeSearchTokens(request.searchTokens)},
      {"hasThumbnail", request.hasThumbnail},
      {"thumbnailMime", request.thumbnailMime},
      {"thumbnailWidth", request.thumbnailWidth},
      {"thumbnailHeight", request.thumbnailHeight},
      {"thumbnailSize", request.thumbnailSize},
  };
  return payload;
}

std::map<std::string, std::string>
decodeValidationErrors(const nlohmann::json &json) {
  std::map<std::string, std::string> errors;
  if (!json.is_object()) {
    return errors;
  }
  for (const auto &[key, value] : json.items()) {
    if (value.is_string()) {
      errors.emplace(key, value.get<std::string>());
    }
  }
  return errors;
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
  return decodeStartUploadResponse(
      client_.postJson("/api/uploads/start", encodeStartUploadRequest(request)));
}

std::vector<BatchStartUploadResult> ArkiveApi::startUploadBatch(
    const std::vector<BatchStartUploadRequest> &requests) {
  nlohmann::json files = nlohmann::json::array();
  for (const auto &item : requests) {
    nlohmann::json payload = encodeStartUploadRequest(item.request);
    payload["clientId"] = item.clientId;
    files.push_back(std::move(payload));
  }

  const auto response = client_.postJson("/api/uploads/batch/start",
                                         {{"files", std::move(files)}});
  std::vector<BatchStartUploadResult> results;
  if (!response.contains("uploads") || !response["uploads"].is_array()) {
    return results;
  }
  for (const auto &item : response["uploads"]) {
    BatchStartUploadResult result{
        .clientId = item.value("clientId", ""),
        .error = item.value("error", ""),
        .validationErrors = decodeValidationErrors(
            item.value("validationErrors", nlohmann::json::object())),
    };
    if (result.error.empty() && result.validationErrors.empty() &&
        item.contains("uploadSessionId")) {
      result.upload = decodeStartUploadResponse(item);
    }
    results.push_back(std::move(result));
  }
  return results;
}

std::string ArkiveApi::presignSingleUpload(
    const std::string &uploadSessionId) {
  return client_.postJson("/api/uploads/" + uploadSessionId + "/presign", {})
      .value("url", "");
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
  client_.postJson("/api/uploads/" + uploadSessionId + "/complete",
                   encodeUploadCompleteRequest(request));
}

std::vector<BatchCompleteUploadResult> ArkiveApi::uploadCompleteBatch(
    const std::vector<BatchCompleteUploadRequest> &requests) {
  nlohmann::json uploads = nlohmann::json::array();
  for (const auto &item : requests) {
    nlohmann::json payload = encodeUploadCompleteRequest(item.request);
    payload["clientId"] = item.clientId;
    payload["uploadSessionId"] = item.uploadSessionId;
    uploads.push_back(std::move(payload));
  }

  const auto response = client_.postJson(
      "/api/uploads/batch/complete", {{"uploads", std::move(uploads)}});
  std::vector<BatchCompleteUploadResult> results;
  if (!response.contains("uploads") || !response["uploads"].is_array()) {
    return results;
  }
  for (const auto &item : response["uploads"]) {
    results.push_back(BatchCompleteUploadResult{
        .clientId = item.value("clientId", ""),
        .completed = item.value("completed", false),
        .error = item.value("error", ""),
    });
  }
  return results;
}

ListSyncEntriesResponse
ArkiveApi::listSyncEntries(const std::optional<std::string> &folderId,
                           bool includeDeleted) {
  return listSyncEntries(ListSyncEntriesRequest{
      .folderId = folderId,
      .includeDeleted = includeDeleted,
  });
}

ListSyncEntriesResponse
ArkiveApi::listSyncEntries(const ListSyncEntriesRequest &request) {
  return decodeListSyncEntriesResponse(
      client_.getJson(buildListSyncEntriesPath(request)));
}

FileRecordResponse ArkiveApi::getFileRecord(const std::string &fileId) {
  return decodeFileRecordResponse(
      client_.getJson("/api/files/" + fileId + "/record"));
}
