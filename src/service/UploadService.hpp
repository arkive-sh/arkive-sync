#pragma once

#include "api/ArkiveApi.hpp"
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

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
  explicit UploadService(ArkiveApi &api);

  UploadFileResponse uploadFile(const UploadFileRequest &request);

private:
  ArkiveApi &api_;
};
