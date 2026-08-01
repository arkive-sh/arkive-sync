#include "sync/RemoteScanner.hpp"

#include "api/ArkiveApi.hpp"
#include "crypto/Aad.hpp"
#include "crypto/RustCrypto.hpp"
#include "helpers/Base64.hpp"
#include "repo/EntryRepo.hpp"
#include "repo/SyncRepo.hpp"
#include "service/VaultService.hpp"

#include <nlohmann/json.hpp>
#include <stdexcept>

RemoteScanner::RemoteScanner(SyncRepo &syncRepo, EntryRepo &entryRepo,
                             ArkiveApi &api, RustCrypto &crypto,
                             VaultService &vaultService)
    : syncRepo_(syncRepo), entryRepo_(entryRepo), api_(api), crypto_(crypto),
      vaultService_(vaultService) {}

void RemoteScanner::scanRoot(const std::string &syncRootId) const {
  const auto syncRoot = syncRepo_.findSyncRootById(syncRootId);
  if (!syncRoot.has_value()) {
    throw std::runtime_error("Sync root is missing");
  }

  const std::optional<std::string> rootRemoteFolderId =
      syncRoot->folderId.empty()
          ? std::nullopt
          : std::optional<std::string>(syncRoot->folderId);
  scanFolder(syncRootId, rootRemoteFolderId, std::filesystem::path());
}

bool RemoteScanner::isRootDeleted(const std::string &syncRootId) const {
  const auto syncRoot = syncRepo_.findSyncRootById(syncRootId);
  if (!syncRoot.has_value()) {
    throw std::runtime_error("Sync root is missing");
  }

  if (syncRoot->folderId.empty()) {
    return false;
  }

  const ListSyncEntriesResponse response = fetchEntries(std::nullopt);
  for (const auto &entry : response.entries) {
    if (entry.remoteId == syncRoot->folderId ||
        (entry.remoteFolderId.has_value() &&
         *entry.remoteFolderId == syncRoot->folderId)) {
      return entry.deletedAt.has_value();
    }
  }

  return false;
}

ListSyncEntriesResponse
RemoteScanner::fetchEntries(const std::optional<std::string> &folderId) const {
  return api_.listSyncEntries(folderId, true);
}

std::optional<std::string> RemoteScanner::decryptEntryName(
    const std::optional<std::string> &encryptedName) const {
  if (!encryptedName.has_value() || encryptedName->empty()) {
    return std::nullopt;
  }

  vaultService_.ensureUnlocked();

  std::vector<uint8_t> encryptedBytes = decodeBase64(*encryptedName);
  const std::vector<uint8_t> plaintext = crypto_.decryptChunk(
      vaultService_.masterKey(), ArkiveAad::toBytes(ArkiveAad::kFolderName),
      encryptedBytes);
  const auto json = nlohmann::json::parse(
      std::string(plaintext.begin(), plaintext.end()));

  if (!json.contains("name") || !json["name"].is_string()) {
    return std::nullopt;
  }

  return json["name"].get<std::string>();
}

void RemoteScanner::scanFolder(
    const std::string &syncRootId,
    const std::optional<std::string> &remoteFolderId,
    const std::filesystem::path &localParentPath) const {
  const ListSyncEntriesResponse response = fetchEntries(remoteFolderId);
  for (const auto &entry : response.entries) {
    const auto existing = entryRepo_.findEntryByRemoteId(syncRootId, entry.remoteId);
    std::optional<std::string> entryName = decryptEntryName(entry.encryptedName);
    std::string localPath;

    if (entryName.has_value()) {
      const std::filesystem::path relativePath =
          localParentPath / std::filesystem::path(*entryName);
      localPath = relativePath.generic_string();
    } else if (existing.has_value() && !existing->relativePath.empty()) {
      localPath = existing->relativePath;
    } else {
      continue;
    }

    entryRepo_.upsertRemoteEntry({
        .syncRootId = syncRootId,
        .remoteId = entry.remoteId,
        .localPath = localPath,
        .remoteType = entry.type,
        .remoteFileId = entry.remoteFileId,
        .remoteFolderId = entry.remoteFolderId,
        .remoteParentFolderId = entry.remoteParentFolderId,
        .encryptedName = entry.encryptedName,
        .encryptedMetadata = entry.encryptedMetadata,
        .remoteDeletedAt = entry.deletedAt,
        .remoteUpdatedAt = entry.updatedAt,
    });

    if (entry.type == "folder" && !entry.deletedAt.has_value() &&
        entry.remoteFolderId.has_value()) {
      scanFolder(syncRootId, entry.remoteFolderId,
                 std::filesystem::path(localPath));
    }
  }
}
