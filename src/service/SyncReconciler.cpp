#include "service/SyncReconciler.hpp"

#include "download/DownloadService.hpp"
#include "fs/FileHasher.hpp"
#include "fs/helpers/PathHelpers.hpp"
#include "repo/EntryRepo.hpp"
#include "repo/SyncRepo.hpp"
#include "sync/SyncPolicy.hpp"
#include "sync/SyncStateClassifier.hpp"

#include <filesystem>
#include <limits>
#include <spdlog/spdlog.h>

SyncReconciler::SyncReconciler(EntryRepo &entryRepo)
    : SyncReconciler(entryRepo, nullptr) {}

SyncReconciler::SyncReconciler(EntryRepo &entryRepo,
                               DownloadService *downloadService)
    : SyncReconciler(entryRepo, downloadService, nullptr) {}

SyncReconciler::SyncReconciler(EntryRepo &entryRepo,
                               DownloadService *downloadService,
                               RustCrypto *crypto)
    : entryRepo_(entryRepo), downloadService_(downloadService),
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

void SyncReconciler::reconcileRoot(const SyncRoot &root) {
  for (const auto &entry : entryRepo_.listEntriesBySyncRootId(root.Id)) {
    const SyncEntryState state = SyncStateClassifier::classify(entry);
    const SyncDecision decision = SyncPolicy::decide(state, root.mode);
    const std::filesystem::path absolutePath =
        std::filesystem::path(normalizeFsPath(root.localPath)) /
        entry.relativePath;

    if (decision == SyncDecision::DeleteLocal) {
      applyDeleteLocal(root, absolutePath, entry.isDirectory);
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
          entryRepo_.markEntryDownloaded(entry.id, static_cast<int64_t>(size),
                                         mtime, hash);
        } else {
          entryRepo_.markEntryDownloaded(entry.id);
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
