#pragma once

#include "api/ArkiveApi.hpp"
#include "fs/FileEncryptor.hpp"
#include "repo/SyncRepo.hpp"
#include <filesystem>
#include <string>

struct UploadFileResponse {
  std::string fileId;
  std::string vaultId;
  std::string uploadSessionId;
  std::string providerUploadId;
};

class UploadService {
public:
  UploadService(ArkiveApi &api, FileEncryptor &fileEncryptor);

  UploadFileResponse uploadFile(const std::filesystem::path &path,
                                const EntryRecord &entry);

private:
  ArkiveApi &api_;
  FileEncryptor &fileEncryptor_;
};
