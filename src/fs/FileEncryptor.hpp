#pragma once

#include "crypto/RustCrypto.hpp"
#include "service/VaultService.hpp"
#include <cstdint>
#include <string>
#include <vector>

class FileEncryptor {
public:
  FileEncryptor(RustCrypto &crypto, VaultService &vaultService);

  std::vector<uint8_t> createFileKey();
  std::vector<uint8_t> wrapFileKey(const std::vector<uint8_t> &fileKey,
                                   const std::string &vaultId,
                                   const std::string &fileId);
  std::vector<uint8_t> encryptMetadata(const std::string &metadataJson,
                                       const std::vector<uint8_t> &fileKey,
                                       const std::string &vaultId,
                                       const std::string &fileId);
  void createEncryptedChunkReader();

private:
  RustCrypto &crypto_;
  VaultService &vaultService_;
};
