#pragma once

#include "api/ArkiveApi.hpp"
#include "crypto/RustCrypto.hpp"
#include "download/DownloadManifest.hpp"
#include "service/VaultService.hpp"

#include <string>
#include <vector>

struct DecryptedDownloadRecord {
  std::vector<uint8_t> fileKey;
  std::string metadataJson;
  DownloadManifest manifest;
};

class DownloadRecordDecryptor {
public:
  DownloadRecordDecryptor(RustCrypto &crypto, VaultService &vaultService);

  DecryptedDownloadRecord decrypt(const FileRecordResponse &record) const;

private:
  RustCrypto &crypto_;
  VaultService &vaultService_;
};
