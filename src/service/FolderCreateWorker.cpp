#include "service/FolderCreateWorker.hpp"

#include "api/ArkiveApi.hpp"
#include "fs/FileEncryptor.hpp"
#include "fs/helpers/PathHelpers.hpp"
#include "helpers/Base64.hpp"
#include "repo/EntryRepo.hpp"
#include "repo/SyncRepo.hpp"
#include "repo/UserRepo.hpp"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace {

std::string folderNameFromPath(const std::string &relativePath) {
  const std::string name = std::filesystem::path(relativePath)
                               .lexically_normal()
                               .filename()
                               .string();
  if (name.empty()) {
    throw std::runtime_error("Queued folder entry has an empty name");
  }
  return name;
}

std::string folderNameFromLocalPath(const std::string &localPath) {
  std::filesystem::path path = normalizeFsPath(localPath);
  std::string name = path.filename().string();
  if (name.empty()) {
    name = path.parent_path().filename().string();
  }
  if (name.empty()) {
    throw std::runtime_error("Sync root folder name is empty");
  }
  return name;
}

} // namespace

FolderCreateWorker::FolderCreateWorker(SyncRepo &syncRepo, EntryRepo &entryRepo,
                                       UserRepo &userRepo,
                                       FileEncryptor &fileEncryptor,
                                       ArkiveApi &api)
    : syncRepo_(syncRepo), entryRepo_(entryRepo), userRepo_(userRepo),
      fileEncryptor_(fileEncryptor), api_(api) {}

std::string FolderCreateWorker::userId() const {
  const auto account = userRepo_.getAccount();
  if (!account.has_value() || !account->userId.has_value() ||
      account->userId->empty()) {
    throw std::runtime_error("User id is missing. Run `arkive-sync login`.");
  }
  return *account->userId;
}

bool FolderCreateWorker::ensureRootFolder(const std::string &syncRootId) {
  const auto syncRoot = syncRepo_.findSyncRootById(syncRootId);
  if (!syncRoot.has_value()) {
    throw std::runtime_error("Sync root is missing");
  }
  if (!syncRoot->folderId.empty()) {
    return false;
  }

  const std::string folderName = folderNameFromLocalPath(syncRoot->localPath);

  const nlohmann::json metadata{{"name", folderName}};

  std::vector<uint8_t> encryptedName;
  std::vector<uint8_t> encryptedMetadata;
  try {
    encryptedName = fileEncryptor_.encryptFolderName(metadata.dump());
    encryptedMetadata = fileEncryptor_.encryptFolderMetadata(metadata.dump());

    const CreateFolderResponse created = api_.createFolder(CreateFolderRequest{
        .parentFolderId = std::nullopt,
        .encryptedName = encodeBase64(encryptedName),
        .encryptedMetadata = encodeBase64(encryptedMetadata),
        .searchTokens =
            fileEncryptor_.createSearchTokenEntries(userId(), folderName),
    });

    syncRepo_.upsertSyncRoot({
        .Id = syncRoot->Id,
        .localPath = syncRoot->localPath,
        .folderId = created.id,
        .enabled = syncRoot->enabled,
    });
  } catch (...) {
    if (!encryptedName.empty()) {
      fileEncryptor_.zeroize(encryptedName);
    }
    if (!encryptedMetadata.empty()) {
      fileEncryptor_.zeroize(encryptedMetadata);
    }
    throw;
  }

  if (!encryptedName.empty()) {
    fileEncryptor_.zeroize(encryptedName);
  }
  if (!encryptedMetadata.empty()) {
    fileEncryptor_.zeroize(encryptedMetadata);
  }

  return true;
}

void FolderCreateWorker::run(const TransferJob &job) {
  if (job.jobType != "create_folder") {
    throw std::invalid_argument(
        "FolderCreateWorker requires create_folder job");
  }

  const auto entry = entryRepo_.getEntryById(job.entryId);
  if (!entry.has_value()) {
    throw std::runtime_error("Queued folder entry is missing");
  }
  if (!entry->isDirectory) {
    throw std::runtime_error("Queued create_folder entry is not a directory");
  }

  const auto syncRoot = syncRepo_.findSyncRootById(entry->syncRootId);
  if (!syncRoot.has_value()) {
    throw std::runtime_error("Queued folder sync root is missing");
  }

  const std::string folderName = folderNameFromPath(entry->relativePath);
  const nlohmann::json metadata{{"name", folderName}};

  std::vector<uint8_t> encryptedName;
  std::vector<uint8_t> encryptedMetadata;
  try {
    encryptedName = fileEncryptor_.encryptFolderName(metadata.dump());
    encryptedMetadata = fileEncryptor_.encryptFolderMetadata(metadata.dump());

    const CreateFolderResponse created = api_.createFolder(CreateFolderRequest{
        .parentFolderId = job.remoteFolderId,
        .encryptedName = encodeBase64(encryptedName),
        .encryptedMetadata = encodeBase64(encryptedMetadata),
        .searchTokens =
            fileEncryptor_.createSearchTokenEntries(userId(), folderName),
    });

    entryRepo_.markFolderCreated(job.entryId, created.id,
                                 created.parentFolderId);
  } catch (...) {
    if (!encryptedName.empty()) {
      fileEncryptor_.zeroize(encryptedName);
    }
    if (!encryptedMetadata.empty()) {
      fileEncryptor_.zeroize(encryptedMetadata);
    }
    throw;
  }

  if (!encryptedName.empty()) {
    fileEncryptor_.zeroize(encryptedName);
  }
  if (!encryptedMetadata.empty()) {
    fileEncryptor_.zeroize(encryptedMetadata);
  }
}
