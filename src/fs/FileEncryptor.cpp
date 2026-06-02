#include "fs/FileEncryptor.hpp"
#include "crypto/Aad.hpp"
#include <cstdint>
#include <stdexcept>
#include <vector>

FileEncryptor::FileEncryptor(RustCrypto &crypto, VaultService &vaultService)
    : crypto_(crypto), vaultService_(vaultService) {}

std::vector<uint8_t> FileEncryptor::createFileKey() {
  return crypto_.generateFileKey();
}

std::vector<uint8_t>
FileEncryptor::wrapFileKey(const std::vector<uint8_t> &fileKey,
                           const std::string &vaultId,
                           const std::string &fileId) {
  if (fileKey.empty()) {
    throw std::invalid_argument("fileKey cannot be empty");
  }

  const std::vector<uint8_t> aad =
      ArkiveAad::toBytes(ArkiveAad::makeFileKey(vaultId, fileId));

  return crypto_.wrapFileKey(fileKey, vaultService_.masterKey(), aad);
}

std::vector<uint8_t>
FileEncryptor::encryptMetadata(const std::string &metadataJson,
                               const std::vector<uint8_t> &fileKey,
                               const std::string &vaultId,
                               const std::string &fileId) {
  if (fileKey.empty()) {
    throw std::invalid_argument("fileKey cannot be empty");
  }

  const std::vector<uint8_t> metadataBytes(metadataJson.begin(),
                                           metadataJson.end());
  const std::vector<uint8_t> aad =
      ArkiveAad::toBytes(ArkiveAad::makeFileMetadata(vaultId, fileId));

  return crypto_.encryptChunk(fileKey, aad, metadataBytes);
}

std::vector<uint8_t>
FileEncryptor::encryptChunk(const std::vector<uint8_t> &plaintextChunk,
                            const std::vector<uint8_t> &fileKey,
                            const std::vector<uint8_t> &aad) {
  if (fileKey.empty()) {
    throw std::invalid_argument("fileKey cannot be empty");
  }

  return crypto_.encryptChunk(fileKey, aad, plaintextChunk);
}
