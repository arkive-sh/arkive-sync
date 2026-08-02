#include "sync/RemoteScanner.hpp"

#include "api/ArkiveApi.hpp"
#include "crypto/Aad.hpp"
#include "crypto/RustCrypto.hpp"
#include "helpers/Base64.hpp"
#include "repo/EntryRepo.hpp"
#include "repo/SyncRepo.hpp"
#include "repo/UserRepo.hpp"
#include "service/VaultService.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <stdexcept>

RemoteScanner::RemoteScanner(SyncRepo &syncRepo, EntryRepo &entryRepo,
                             ArkiveApi &api, RustCrypto &crypto,
                             VaultService &vaultService, UserRepo &userRepo)
    : syncRepo_(syncRepo), entryRepo_(entryRepo), api_(api), crypto_(crypto),
      vaultService_(vaultService), userRepo_(userRepo) {}

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
    const SyncEntryResponse &entry) const {
  if (entry.type == "folder" &&
      (!entry.encryptedName.has_value() || entry.encryptedName->empty())) {
    return std::nullopt;
  }

  vaultService_.ensureUnlocked();

  std::vector<uint8_t> plaintext;
  if (entry.type == "folder") {
    plaintext = crypto_.decryptChunk(
        vaultService_.masterKey(), ArkiveAad::toBytes(ArkiveAad::kFolderName),
        decodeBase64(*entry.encryptedName));
  } else if (entry.type == "file" && entry.encryptedMetadata.has_value() &&
             entry.encryptedFileKey.has_value()) {
    const auto account = userRepo_.getAccount();
    if (!account.has_value() || !account->userId.has_value() ||
        account->userId->empty()) {
      return std::nullopt;
    }

    const std::vector<uint8_t> fileKey = crypto_.unwrapFileKey(
        decodeBase64(*entry.encryptedFileKey), vaultService_.masterKey(),
        ArkiveAad::toBytes(ArkiveAad::makeFileKey(*account->userId,
                                                  entry.remoteId)));
    plaintext = crypto_.decryptChunk(
        fileKey,
        ArkiveAad::toBytes(
            ArkiveAad::makeFileMetadata(*account->userId, entry.remoteId)),
        decodeBase64(*entry.encryptedMetadata));
  } else {
    return std::nullopt;
  }

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
    std::optional<std::string> entryName = decryptEntryName(entry);
    std::string localPath;

    if (entryName.has_value()) {
      const std::filesystem::path relativePath =
          localParentPath / std::filesystem::path(*entryName);
      localPath = relativePath.generic_string();
    } else if (existing.has_value() && !existing->relativePath.empty()) {
      localPath = existing->relativePath;
    } else {
      spdlog::error("Skipping remote entry root={} remote_id={} type={}: "
                    "missing decrypted name and existing path",
                    syncRootId, entry.remoteId, entry.type);
      continue;
    }

    spdlog::debug("Remote scan entry root={} remote_id={} type={} path={}",
                  syncRootId, entry.remoteId, entry.type, localPath);

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
