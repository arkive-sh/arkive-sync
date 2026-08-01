#include "download/DownloadRecordDecryptor.hpp"

#include "crypto/Aad.hpp"
#include "helpers/Base64.hpp"

#include <stdexcept>

DownloadRecordDecryptor::DownloadRecordDecryptor(RustCrypto &crypto,
                                                 VaultService &vaultService)
    : crypto_(crypto), vaultService_(vaultService) {}

DecryptedDownloadRecord
DownloadRecordDecryptor::decrypt(const FileRecordResponse &record) const {
  vaultService_.ensureUnlocked();

  std::vector<uint8_t> fileKey = crypto_.unwrapFileKey(
      decodeBase64(record.encryptedFileKey), vaultService_.masterKey(),
      ArkiveAad::toBytes(ArkiveAad::makeFileKey(record.vaultId, record.fileId)));

  const auto metadataBytes = crypto_.decryptFileMetadata(
      decodeBase64(record.encryptedMetadata), fileKey,
      ArkiveAad::toBytes(
          ArkiveAad::makeFileMetadata(record.vaultId, record.fileId)));
  const auto manifestBytes = crypto_.decryptChunk(
      fileKey,
      ArkiveAad::toBytes(
          ArkiveAad::makeFileManifest(record.vaultId, record.fileId)),
      decodeBase64(record.encryptedManifest));

  const std::string metadataJson(metadataBytes.begin(), metadataBytes.end());
  const std::string manifestJson(manifestBytes.begin(), manifestBytes.end());

  return DecryptedDownloadRecord{
      .fileKey = std::move(fileKey),
      .metadataJson = metadataJson,
      .manifest = parseDownloadManifest(manifestJson, record.chunkSize,
                                        record.plaintextSize),
  };
}
