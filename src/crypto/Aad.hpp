#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ArkiveAad {

inline constexpr std::string_view kMasterKey = "arkive:master-key:v1";
inline constexpr std::string_view kSessionMasterKey =
    "arkive:session-master-key:v1";
inline constexpr std::string_view kFolderName = "arkive:folder-name:v1";
inline constexpr std::string_view kFolderMetadata =
    "arkive:folder-metadata:v1";
inline constexpr std::string_view kFileKeyPrefix = "arkive:file-key:v1:";
inline constexpr std::string_view kFileMetadataPrefix =
    "arkive:file-metadata:v1:";
inline constexpr std::string_view kFileManifestPrefix =
    "arkive:file-manifest:v1:";
inline constexpr std::string_view kFileThumbnailPrefix =
    "arkive:file-thumbnail:v1:";
inline constexpr std::string_view kFileChunkPrefix = "arkive:file-chunk:v1:";
inline constexpr std::string_view kShareKeyPrefix = "arkive:share-key:v1:";
inline constexpr std::string_view kShareFileKeyPrefix =
    "arkive:share-file-key:v1:";
inline constexpr std::string_view kMasterKeyRecoveryPrefix =
    "arkive:master-key:recovery:v1:";
inline constexpr std::string_view kMasterKeyPasswordPrefix =
    "arkive:master-key:password:v1:";

std::vector<uint8_t> toBytes(std::string_view value);

std::string makeFileKey(const std::string &vaultId, const std::string &fileId);
std::string makeFileMetadata(const std::string &vaultId,
                             const std::string &fileId);
std::string makeFileManifest(const std::string &vaultId,
                             const std::string &fileId);
std::string makeFileThumbnail(const std::string &vaultId,
                              const std::string &fileId);
std::string makeFileChunk(const std::string &vaultId, const std::string &fileId,
                          int chunkIndex, int64_t chunkSize, int totalChunks);
std::string makeShareKey(const std::string &token);
std::string makeShareFileKey(const std::string &fileId,
                             const std::string &token);
std::string makeMasterKeyRecovery(const std::string &userId);
std::string makeMasterKeyPassword(const std::string &userId);

} // namespace ArkiveAad
