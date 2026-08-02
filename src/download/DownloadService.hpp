#pragma once

#include "api/ArkiveApi.hpp"
#include "api/ArkiveHttpClient.hpp"
#include "crypto/RustCrypto.hpp"
#include "download/DownloadRecordDecryptor.hpp"

#include <filesystem>

class DownloadService {
public:
  DownloadService(ArkiveApi &api, ArkiveHttpClient &httpClient,
                  RustCrypto &crypto,
                  DownloadRecordDecryptor &recordDecryptor);

  void downloadFile(const std::string &fileId,
                    const std::filesystem::path &targetPath) const;

private:
  ArkiveApi &api_;
  ArkiveHttpClient &httpClient_;
  RustCrypto &crypto_;
  DownloadRecordDecryptor &recordDecryptor_;
};
