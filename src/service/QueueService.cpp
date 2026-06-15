#include "service/QueueService.hpp"

#include "repo/EntryRepo.hpp"
#include "repo/QueueRepo.hpp"
#include "repo/SyncRepo.hpp"
#include "service/FolderCreateWorker.hpp"
#include "service/UploadJobRunner.hpp"

#include <filesystem>
#include <optional>
#include <spdlog/spdlog.h>
#include <stdexcept>

QueueService::QueueService(EntryRepo &entryRepo, QueueRepo &queueRepo,
                           SyncRepo &syncRepo,
                           FolderCreateWorker *folderCreateWorker,
                           UploadJobRunner *uploadJobRunner)
    : entryRepo_(entryRepo), queueRepo_(queueRepo), syncRepo_(syncRepo),
      folderCreateWorker_(folderCreateWorker),
      uploadJobRunner_(uploadJobRunner) {}

void QueueService::build(const std::string &syncRootId) {
  const auto syncRoot = syncRepo_.findSyncRootById(syncRootId);
  if (!syncRoot.has_value()) {
    return;
  }

  for (const auto &entry :
       entryRepo_.listPendingUploadDirectoriesBySyncRootId(syncRootId)) {
    if (entry.remoteId.has_value() ||
        queueRepo_.hasActiveCreateFolderForEntry(entry.id)) {
      continue;
    }

    std::optional<std::string> remoteFolderId;
    const std::filesystem::path parentPath =
        std::filesystem::path(entry.relativePath).parent_path();
    if (parentPath.empty()) {
      if (!syncRoot->folderId.empty()) {
        remoteFolderId = syncRoot->folderId;
      }
    } else {
      const auto parentEntry =
          entryRepo_.findEntryByPath(syncRootId, parentPath.generic_string());
      if (!parentEntry.has_value() || !parentEntry->remoteId.has_value()) {
        continue;
      }

      remoteFolderId = parentEntry->remoteId;
    }

    queueRepo_.enqueueCreateFolder(entry.id, entry.relativePath, remoteFolderId);
  }

  for (const auto &entry :
       entryRepo_.listPendingUploadFilesBySyncRootId(syncRootId)) {
    if (queueRepo_.hasActiveUploadFileForEntry(entry.id) ||
        !entry.size.has_value() || *entry.size < 0) {
      continue;
    }

    std::optional<std::string> remoteFolderId;
    const std::filesystem::path parentPath =
        std::filesystem::path(entry.relativePath).parent_path();
    if (parentPath.empty()) {
      if (!syncRoot->folderId.empty()) {
        remoteFolderId = syncRoot->folderId;
      }
    } else {
      const auto parentEntry =
          entryRepo_.findEntryByPath(syncRootId, parentPath.generic_string());
      if (!parentEntry.has_value() || !parentEntry->remoteId.has_value()) {
        continue;
      }

      remoteFolderId = parentEntry->remoteId;
    }

    queueRepo_.enqueueUploadFile(entry.id, entry.relativePath, remoteFolderId,
                                 static_cast<uint64_t>(*entry.size));
  }
}

void QueueService::runTick() {
  if (folderCreateWorker_ == nullptr) {
    return;
  }

  while (true) {
    const auto job = queueRepo_.claimNextQueuedByType("create_folder");
    if (!job.has_value()) {
      break;
    }

    const auto entry = entryRepo_.getEntryById(job->entryId);
    const std::string syncRootId =
        entry.has_value() ? entry->syncRootId : std::string();

    try {
      folderCreateWorker_->run(*job);
      queueRepo_.markDone(job->id);
      if (!syncRootId.empty()) {
        build(syncRootId);
      }
    } catch (const std::exception &error) {
      queueRepo_.markFailed(job->id, error.what());
      spdlog::error("Queue job {} failed: {}", job->id, error.what());
    } catch (...) {
      queueRepo_.markFailed(job->id, "Unknown queue job error");
      spdlog::error("Queue job {} failed with unknown error", job->id);
    }
  }

  if (uploadJobRunner_ == nullptr) {
    return;
  }

  while (true) {
    const auto job = queueRepo_.claimNextQueuedByType("upload_file");
    if (!job.has_value()) {
      break;
    }

    const auto entry = entryRepo_.getEntryById(job->entryId);
    const std::string syncRootId =
        entry.has_value() ? entry->syncRootId : std::string();

    try {
      uploadJobRunner_->run(*job);
      queueRepo_.markDone(job->id);
      if (!syncRootId.empty()) {
        build(syncRootId);
      }
    } catch (const std::exception &error) {
      queueRepo_.markFailed(job->id, error.what());
      spdlog::error("Queue job {} failed: {}", job->id, error.what());
    } catch (...) {
      queueRepo_.markFailed(job->id, "Unknown queue job error");
      spdlog::error("Queue job {} failed with unknown error", job->id);
    }
  }
}
