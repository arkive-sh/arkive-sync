#include "crypto/Aad.hpp"

namespace {

std::string appendScopedAad(std::string_view prefix,
                            std::initializer_list<std::string_view> parts) {
  std::string value(prefix);
  for (const std::string_view part : parts) {
    value.append(part);
    value.push_back(':');
  }

  if (!parts.size()) {
    return value;
  }

  value.pop_back();
  return value;
}

} // namespace

namespace ArkiveAad {

std::vector<uint8_t> toBytes(std::string_view value) {
  return std::vector<uint8_t>(value.begin(), value.end());
}

std::string makeFileKey(const std::string &vaultId, const std::string &fileId) {
  return appendScopedAad(kFileKeyPrefix, {vaultId, fileId});
}

std::string makeFileMetadata(const std::string &vaultId,
                             const std::string &fileId) {
  return appendScopedAad(kFileMetadataPrefix, {vaultId, fileId});
}

std::string makeFileManifest(const std::string &vaultId,
                             const std::string &fileId) {
  return appendScopedAad(kFileManifestPrefix, {vaultId, fileId});
}

std::string makeFileThumbnail(const std::string &vaultId,
                              const std::string &fileId) {
  return appendScopedAad(kFileThumbnailPrefix, {vaultId, fileId});
}

std::string makeFileChunk(const std::string &vaultId, const std::string &fileId,
                          int chunkIndex, int64_t chunkSize, int totalChunks) {
  return appendScopedAad(
      kFileChunkPrefix,
      {vaultId, fileId, std::to_string(chunkIndex), std::to_string(chunkSize),
       std::to_string(totalChunks)});
}

std::string makeShareKey(const std::string &token) {
  return appendScopedAad(kShareKeyPrefix, {token});
}

std::string makeShareFileKey(const std::string &fileId,
                             const std::string &token) {
  return appendScopedAad(kShareFileKeyPrefix, {fileId, token});
}

std::string makeMasterKeyRecovery(const std::string &userId) {
  return appendScopedAad(kMasterKeyRecoveryPrefix, {userId});
}

std::string makeMasterKeyPassword(const std::string &userId) {
  return appendScopedAad(kMasterKeyPasswordPrefix, {userId});
}

std::string makeLocalPath(const std::string &scopeId,
                          const std::string &pathHash) {
  return appendScopedAad(kLocalPathPrefix, {scopeId, pathHash});
}

std::string makeResumeFileKey(const std::string &uploadSessionId) {
  return appendScopedAad(kResumeFileKeyPrefix, {uploadSessionId});
}

} // namespace ArkiveAad
