#include "service/SyncReconciler.hpp"

#include "download/DownloadService.hpp"
#include "fs/FileHasher.hpp"
#include "fs/helpers/PathHelpers.hpp"
#include "repo/ConflictRepo.hpp"
#include "repo/EntryRepo.hpp"
#include "repo/RemoteEntryRepo.hpp"
#include "repo/SyncRepo.hpp"
#include "sync/SyncPolicy.hpp"
#include "sync/SyncStateClassifier.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <spdlog/spdlog.h>
#include <sstream>
#include <stdexcept>

SyncReconciler::SyncReconciler(EntryRepo &entryRepo)
    : entryRepo_(entryRepo) {}

SyncReconciler::SyncReconciler(EntryRepo &entryRepo,
                               DownloadService *downloadService)
    : entryRepo_(entryRepo), downloadService_(downloadService) {}

SyncReconciler::SyncReconciler(EntryRepo &entryRepo,
                               ConflictRepo &conflictRepo,
                               RemoteEntryRepo &remoteEntryRepo,
                               DownloadService *downloadService,
                               RustCrypto *crypto)
    : entryRepo_(entryRepo), conflictRepo_(&conflictRepo),
      remoteEntryRepo_(&remoteEntryRepo), downloadService_(downloadService),
      crypto_(crypto) {}

void SyncReconciler::applyDeleteLocal(const SyncRoot &root,
                                      const std::filesystem::path &path,
                                      bool isDirectory) {
  std::error_code error;
  spdlog::info("Deleting local {} for root {} path {}",
               isDirectory ? "folder" : "file", root.Id, path.string());

  if (isDirectory) {
    std::filesystem::remove_all(path, error);
  } else {
    std::filesystem::remove(path, error);
  }

  if (error) {
    spdlog::error("Failed to delete local path for root {} path {}: {}",
                  root.Id, path.string(), error.message());
  } else {
    spdlog::info("Deleted local {} for root {} path {}",
                 isDirectory ? "folder" : "file", root.Id, path.string());
  }
}

std::filesystem::path
SyncReconciler::conflictPathFor(const std::filesystem::path &path) const {
  const auto now = std::chrono::system_clock::now();
  const std::time_t rawTime = std::chrono::system_clock::to_time_t(now);
  std::tm time{};
  localtime_r(&rawTime, &time);

  std::ostringstream suffix;
  suffix << " (remote conflict " << std::put_time(&time, "%Y%m%d-%H%M%S")
         << ")";

  const std::filesystem::path parent = path.parent_path();
  const std::string stem = path.stem().string();
  const std::string extension = path.extension().string();

  for (int index = 1; index < 1000; ++index) {
    std::string name = stem + suffix.str();
    if (index > 1) {
      name += "-" + std::to_string(index);
    }
    name += extension;

    const auto candidate = parent / name;
    if (!std::filesystem::exists(candidate)) {
      return candidate;
    }
  }

  throw std::runtime_error("could not allocate conflict filename");
}

void SyncReconciler::applyConflict(const SyncRoot &root, const Entry &entry,
                                   const std::filesystem::path &path) {
  if (conflictRepo_ != nullptr) {
    conflictRepo_->markConflict(entry.id, "local_remote_modified",
                                "local and remote changed since last sync");
  }

  if (entry.isDirectory) {
    spdlog::info("Conflict detected for folder root={} path={}", root.Id,
                 entry.relativePath);
    return;
  }

  if (entry.remoteDeletedAt.has_value()) {
    spdlog::info("Conflict detected root={} path={} remote_deleted=true",
                 root.Id, entry.relativePath);
    return;
  }

  if (downloadService_ == nullptr || !entry.remoteFileId.has_value()) {
    spdlog::error("Conflict detected for root {} path {}, but remote copy "
                  "cannot be downloaded",
                  root.Id, entry.relativePath);
    return;
  }

  const auto conflictPath = conflictPathFor(path);
  try {
    downloadService_->downloadFile(*entry.remoteFileId, conflictPath);
    spdlog::info("Conflict detected root={} path={} remote_copy={}", root.Id,
                 entry.relativePath, conflictPath.string());
  } catch (const std::exception &error) {
    spdlog::error("Failed to download conflict copy for root {} path {}: {}",
                  root.Id, entry.relativePath, error.what());
  }
}

void SyncReconciler::reconcileRoot(const SyncRoot &root) {
  for (const auto &entry : entryRepo_.listEntriesBySyncRootId(root.Id)) {
    const SyncEntryState state = SyncStateClassifier::classify(entry);
    const SyncDecision decision = SyncPolicy::decide(state, root.mode);
    const std::filesystem::path absolutePath =
        std::filesystem::path(normalizeFsPath(root.localPath)) /
        entry.relativePath;

    if (decision == SyncDecision::DeleteLocal) {
      applyDeleteLocal(root, absolutePath, entry.isDirectory);
    } else if (decision == SyncDecision::Conflict) {
      applyConflict(root, entry, absolutePath);
    } else if (decision == SyncDecision::Download && entry.isDirectory) {
      spdlog::debug("Skipping folder download for root {} path {}", root.Id,
                    entry.relativePath);
    } else if (decision == SyncDecision::Download && downloadService_ == nullptr) {
      spdlog::error("Cannot download remote file for root {} path {}: "
                    "download service is unavailable",
                    root.Id, entry.relativePath);
    } else if (decision == SyncDecision::Download && !entry.remoteFileId.has_value()) {
      spdlog::error("Cannot download remote file for root {} path {}: "
                    "remote_file_id is missing",
                    root.Id, entry.relativePath);
    } else if (decision == SyncDecision::Download) {
      try {
        downloadService_->downloadFile(*entry.remoteFileId, absolutePath);
        if (remoteEntryRepo_ == nullptr) {
          throw std::runtime_error("remote entry repo is unavailable");
        }

        if (crypto_ != nullptr) {
          std::error_code ec;
          const auto size = std::filesystem::file_size(absolutePath, ec);
          if (ec || size >
                        static_cast<uintmax_t>(
                            std::numeric_limits<int64_t>::max())) {
            throw std::runtime_error("downloaded file size unavailable");
          }

          const auto mtime = std::filesystem::last_write_time(absolutePath, ec);
          if (ec) {
            throw std::runtime_error("downloaded file mtime unavailable");
          }

          const std::string hash = FileHasher(absolutePath, *crypto_).hashFile();
          remoteEntryRepo_->markEntryDownloaded(
              entry.id, static_cast<int64_t>(size), mtime, hash);
        } else {
          remoteEntryRepo_->markEntryDownloaded(entry.id);
        }
        spdlog::info("Downloaded remote file for root {} path {}", root.Id,
                     entry.relativePath);
      } catch (const std::exception &error) {
        spdlog::error("Failed to download remote file for root {} path {}: {}",
                      root.Id, entry.relativePath, error.what());
      }
    }

    const bool noisyDecision =
        decision == SyncDecision::Noop ||
        (decision == SyncDecision::Download && entry.isDirectory);
    if (noisyDecision) {
      spdlog::debug("sync reconcile root={} path={} mode={} decision={} "
                    "local_dirty={} remote_dirty={} conflict={}",
                    root.Id, entry.relativePath, toSyncModeDb(root.mode),
                    toSyncDecisionName(decision), state.localDirty,
                    state.remoteDirty, state.hasConflict);
    } else {
      spdlog::info("sync reconcile root={} path={} mode={} decision={} "
                   "local_dirty={} remote_dirty={} conflict={}",
                   root.Id, entry.relativePath, toSyncModeDb(root.mode),
                   toSyncDecisionName(decision), state.localDirty,
                   state.remoteDirty, state.hasConflict);
    }
  }
}
