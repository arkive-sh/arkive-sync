#include "service/UploadService.hpp"
#include <algorithm>
#include <stdexcept>

UploadService::UploadService(ArkiveApi &api) : api_(api) {}

UploadFileResponse UploadService::uploadFile(const UploadFileRequest &request) {
  if (request.parts.empty()) {
    throw std::invalid_argument("uploadFile requires at least one part");
  }

  const StartUploadResponse started = api_.startUpload(request.start);

  std::vector<int> partNumbers;
  partNumbers.reserve(request.parts.size());
  for (const auto &part : request.parts) {
    partNumbers.push_back(part.partNumber);
  }

  const PresignPartsResponse presigned =
      api_.presignParts(started.uploadSessionId, partNumbers);

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

    const std::string etag =
        api_.putEncryptedPartToStorage(urlIt->second, part.body);
    api_.uploadPart(started.uploadSessionId,
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
        api_.presignThumbnail(started.uploadSessionId, *request.thumbnail);
    api_.putEncryptedPartToStorage(thumbnailUrl,
                                   request.encryptedThumbnailBody);
  } else if (!request.encryptedThumbnailBody.empty()) {
    throw std::invalid_argument(
        "uploadFile received thumbnail bytes without thumbnail metadata");
  }

  api_.uploadComplete(started.uploadSessionId, request.complete);

  return UploadFileResponse{
      .fileId = started.fileId,
      .vaultId = started.vaultId,
      .uploadSessionId = started.uploadSessionId,
      .providerUploadId = started.providerUploadId,
  };
}
