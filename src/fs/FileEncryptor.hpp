#pragma once

#include "api/ArkiveApi.hpp"
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
  std::vector<uint8_t> encryptFolderName(const std::string &metadataJson);
  std::vector<uint8_t> encryptFolderMetadata(const std::string &metadataJson);
  std::vector<uint8_t> encryptChunk(const std::vector<uint8_t> &plaintextChunk,
                                    const std::vector<uint8_t> &fileKey,
                                    const std::vector<uint8_t> &aad);
  std::vector<uint8_t>
  encryptResumeFileKey(const std::vector<uint8_t> &fileKey,
                       const std::string &uploadSessionId);
  std::vector<uint8_t>
  decryptResumeFileKey(const std::vector<uint8_t> &encryptedFileKeyBlob,
                       const std::string &uploadSessionId);
  std::vector<uint8_t> hashBytes(const std::vector<uint8_t> &bytes);
  std::vector<UploadCompleteSearchToken>
  createSearchTokenEntries(const std::string &vaultId,
                           const std::string &name,
                           const std::string &mime);
  void zeroize(std::vector<uint8_t> &bytes);

private:
  RustCrypto &crypto_;
  VaultService &vaultService_;
};
