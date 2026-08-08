#include "service/QueueService.hpp"

#include "repo/EntryRepo.hpp"
#include "repo/QueueRepo.hpp"
#include "repo/SyncRepo.hpp"
#include "service/FolderCreateWorker.hpp"
#include "service/UploadJobRunner.hpp"
#include "sync/SyncPolicy.hpp"
#include "sync/SyncStateClassifier.hpp"

#include <filesystem>
#include <future>
#include <optional>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <vector>

namespace {

constexpr size_t kMaxConcurrentUploads = 4;
constexpr int kQueueBuildPageSize = 500;

struct UploadResult {
  TransferJob job;
  std::exception_ptr error;
};

std::optional<std::string>
resolveRemoteFolderId(EntryRepo &entryRepo, const SyncRoot &syncRoot,
                      const Entry &entry) {
  const std::filesystem::path parentPath =
      std::filesystem::path(entry.relativePath).parent_path();
  if (parentPath.empty()) {
    if (!syncRoot.folderId.empty()) {
      return syncRoot.folderId;
    }
    return std::nullopt;
  }

  const auto parentEntry =
      entryRepo.findEntryByPath(syncRoot.Id, parentPath.generic_string());
  if (!parentEntry.has_value() || !parentEntry->remoteId.has_value()) {
    return std::nullopt;
  }

  return parentEntry->remoteId;
}

} // namespace

QueueService::QueueService(EntryRepo &entryRepo, QueueRepo &queueRepo,
                           SyncRepo &syncRepo,
                           FolderCreateWorker *folderCreateWorker,
                           UploadJobRunner *uploadJobRunner)
    : entryRepo_(entryRepo), queueRepo_(queueRepo), syncRepo_(syncRepo),
      folderCreateWorker_(folderCreateWorker),
      uploadJobRunner_(uploadJobRunner) {}

void QueueService::build(const std::string &syncRootId) {
  std::optional<std::string> afterPath;
  while (true) {
    const QueueBuildPage page = buildPage(syncRootId, afterPath, true);
    if (page.complete) {
      return;
    }
    afterPath = page.nextPath;
  }
}

QueueBuildPage QueueService::buildPage(
    const std::string &syncRootId, const std::optional<std::string> &afterPath,
    bool scanComplete) {
  if (folderCreateWorker_ != nullptr) {
    folderCreateWorker_->ensureRootFolder(syncRootId);
  }

  const auto syncRoot = syncRepo_.findSyncRootById(syncRootId);
  if (!syncRoot.has_value()) {
    return QueueBuildPage{.complete = true};
  }

  const auto entries = entryRepo_.listEntriesBySyncRootIdAfterPath(
      syncRootId, afterPath, kQueueBuildPageSize);
  for (const auto &entry : entries) {
    enqueueEntry(*syncRoot, entry);
  }

  if (entries.empty()) {
    return QueueBuildPage{.complete = scanComplete};
  }

  const auto nextPath = entries.back().relativePath;
  return QueueBuildPage{
      .nextPath = nextPath,
      .complete = scanComplete &&
                  entries.size() < static_cast<size_t>(kQueueBuildPageSize)};
}

void QueueService::enqueueEntry(const std::string &syncRootId,
                                const std::string &relativePath) {
  const auto syncRoot = syncRepo_.findSyncRootById(syncRootId);
  if (!syncRoot.has_value()) {
    return;
  }

  const auto entry = entryRepo_.findEntryByPath(syncRootId, relativePath);
  if (entry.has_value()) {
    enqueueEntry(*syncRoot, *entry);
  }
}

void QueueService::enqueueEntry(const SyncRoot &syncRoot,
                                const Entry &entry) {
  const SyncEntryState state = SyncStateClassifier::classify(entry);
  if (SyncPolicy::decide(state, syncRoot.mode) != SyncDecision::Upload) {
    return;
  }

  const std::optional<std::string> remoteFolderId =
      resolveRemoteFolderId(entryRepo_, syncRoot, entry);
  if (!remoteFolderId.has_value() && !syncRoot.folderId.empty() &&
      std::filesystem::path(entry.relativePath).parent_path().empty()) {
    // root-level children can still target the root folder id
  } else if (!remoteFolderId.has_value() &&
             !std::filesystem::path(entry.relativePath)
                  .parent_path()
                  .empty()) {
    return;
  }

  if (entry.isDirectory) {
    if (entry.remoteId.has_value() ||
        queueRepo_.hasActiveCreateFolderForEntry(entry.id)) {
      return;
    }

    queueRepo_.enqueueCreateFolder(entry.id, entry.relativePath,
                                   remoteFolderId);
    return;
  }

  if (queueRepo_.hasActiveUploadFileForEntry(entry.id) ||
      !entry.size.has_value() || *entry.size < 0) {
    return;
  }

  queueRepo_.enqueueUploadFile(
      entry.id, entry.relativePath, remoteFolderId,
      static_cast<uint64_t>(*entry.size));
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

    try {
      folderCreateWorker_->run(*job);
      queueRepo_.markDone(job->id);
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
    std::vector<std::future<UploadResult>> uploads;
    uploads.reserve(kMaxConcurrentUploads);

    for (size_t i = 0; i < kMaxConcurrentUploads; ++i) {
      const auto job = queueRepo_.claimNextQueuedByType("upload_file");
      if (!job.has_value()) {
        break;
      }

      uploads.push_back(std::async(
          std::launch::async, [this, job = *job]() -> UploadResult {
            try {
              uploadJobRunner_->run(job);
              return UploadResult{.job = job, .error = nullptr};
            } catch (...) {
              return UploadResult{.job = job,
                                  .error = std::current_exception()};
            }
          }));
    }

    if (uploads.empty()) {
      break;
    }

    for (auto &upload : uploads) {
      UploadResult result = upload.get();
      if (result.error == nullptr) {
        queueRepo_.markDone(result.job.id);
        continue;
      }

      try {
        std::rethrow_exception(result.error);
      } catch (const std::exception &error) {
        queueRepo_.markFailed(result.job.id, error.what());
        spdlog::error("Queue job {} failed: {}", result.job.id,
                      error.what());
      } catch (...) {
        queueRepo_.markFailed(result.job.id, "Unknown queue job error");
        spdlog::error("Queue job {} failed", result.job.id);
      }
    }
  }
}
