#include "service/FolderCreateWorker.hpp"

#include "api/ArkiveApi.hpp"
#include "fs/FileEncryptor.hpp"
#include "helpers/Base64.hpp"
#include "repo/EntryRepo.hpp"
#include "repo/SyncRepo.hpp"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace {

std::string folderNameFromPath(const std::string &relativePath) {
  const std::string name =
      std::filesystem::path(relativePath).filename().string();
  if (name.empty()) {
    throw std::runtime_error("Queued folder entry has an empty name");
  }
  return name;
}

} // namespace

FolderCreateWorker::FolderCreateWorker(SyncRepo &syncRepo, EntryRepo &entryRepo,
                                       FileEncryptor &fileEncryptor,
                                       ArkiveApi &api)
    : syncRepo_(syncRepo), entryRepo_(entryRepo), fileEncryptor_(fileEncryptor),
      api_(api) {}

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
        .searchTokens = {},
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
