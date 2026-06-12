#include "./RootScanner.hpp"
#include "fs/FileHasher.hpp"
#include "fs/FileWatcher.hpp"
#include "helpers/GenUUID.hpp"
#include "repo/ScanRepo.hpp"
#include "repo/SyncRepo.hpp"
#include "service/SyncService.hpp"
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>

static constexpr int kMaxScanBatchSize = 500;

RootScanner::RootScanner(RustCrypto &crypto, SyncService &syncSvc,
                         ScanRepo &scanRepo,
                         DirtyPathRepo &dirtyPathRepo)
    : watcher_(IFileWatcher::create()), crypto_(crypto), syncSvc_(syncSvc),
      scanRepo_(scanRepo), dirtyPathRepo_(dirtyPathRepo) {}

RootScanner::RootScanner(RustCrypto &crypto, SyncService &syncSvc,
                         ScanRepo &scanRepo,
                         DirtyPathRepo &dirtyPathRepo,
                         std::unique_ptr<IFileWatcher> watcher)
    : watcher_(std::move(watcher)), crypto_(crypto), syncSvc_(syncSvc),
      scanRepo_(scanRepo), dirtyPathRepo_(dirtyPathRepo) {}

bool RootScanner::scanRoot(const std::string &syncRootId) {
  auto syncRoot = syncSvc_.findSyncRootById(syncRootId);
  if (!syncRoot || !syncRoot->enabled) {
    return false;
  }

  std::error_code ec;
  std::filesystem::path rootPath =
      std::filesystem::absolute(syncRoot->localPath, ec);

  if (ec || !std::filesystem::is_directory(rootPath, ec)) {
    return false;
  }

  auto scanJob = scanRepo_.getScanJob(syncRootId);

  ScanJob job;
  if (scanJob) {
    job = *scanJob;
  } else {
    job = ScanJob{
        .id = generateUUID(),
        .syncRootId = syncRootId,
        .status = "running",
        .cursorPath = std::nullopt,
    };

    if (!scanRepo_.insertScanJob(job)) {
      return false;
    }
  }

  std::filesystem::recursive_directory_iterator it(
      rootPath, std::filesystem::directory_options::skip_permission_denied, ec);

  if (ec) {
    return false;
  }

  std::filesystem::recursive_directory_iterator end;

  bool cursorFound = !job.cursorPath.has_value();

  size_t processed = 0;
  std::optional<std::string> lastCursor;

  while (it != end && processed < kMaxScanBatchSize) {
    std::filesystem::path absPath = it->path();

    std::filesystem::path relPath =
        std::filesystem::relative(absPath, rootPath, ec);
    if (ec) {
      it.increment(ec);
      continue;
    }

    std::string rel = relPath.generic_string();

    if (!cursorFound) {
      if (rel == *job.cursorPath) {
        cursorFound = true;
      }

      it.increment(ec);
      continue;
    }

    auto status = it->symlink_status(ec);
    if (ec) {
      it.increment(ec);
      continue;
    }

    if (std::filesystem::is_symlink(status)) {
      it.disable_recursion_pending();
      it.increment(ec);
      continue;
    }

    if (std::filesystem::is_directory(status)) {
      scanRepo_.upsertDirectoryEntry({
          .syncRootId = syncRootId,
          .relativePath = rel,
          .lastSeenScanId = job.id,
      });

      // Later: daemon/watcher can add watch for this directory.
      lastCursor = rel;
      processed++;

      it.increment(ec);
      continue;
    }

    if (std::filesystem::is_regular_file(status)) {
      auto fileSize = std::filesystem::file_size(absPath, ec);
      if (ec) {
        it.increment(ec);
        continue;
      }

      auto mtime = std::filesystem::last_write_time(absPath, ec);
      if (ec) {
        it.increment(ec);
        continue;
      }

      auto existing = scanRepo_.findEntryByPath(syncRootId, rel);

      std::optional<std::string> contentHash;
      bool shouldHash = true;

      if (existing && !existing->deleted && existing->size == fileSize &&
          existing->mtime == mtime) {
        shouldHash = false;
        contentHash = existing->contentHash;
      }

      if (shouldHash) {
        contentHash = FileHasher(absPath, crypto_).hashFile();
      }

      std::string syncState = "unchanged";

      if (!existing || existing->deleted) {
        syncState = "pending_upload";
      } else if (shouldHash && existing->contentHash != contentHash) {
        syncState = "pending_upload";
      }

      scanRepo_.upsertFileEntry({
          .syncRootId = syncRootId,
          .relativePath = rel,
          .size = fileSize,
          .mtime = mtime,
          .contentHash = *contentHash,
          .syncState = syncState,
          .lastSeenScanId = job.id,
      });

      lastCursor = rel;
      processed++;

      it.increment(ec);
      continue;
    }

    it.disable_recursion_pending();
    it.increment(ec);
  }

  if (it == end) {
    scanRepo_.markScanComplete(job.id);
    scanRepo_.markEntriesNotSeenInScanDeleted(syncRootId, job.id);
    return true;
  }

  if (lastCursor.has_value()) {
    scanRepo_.updateScanCursor(job.id, *lastCursor);
  }

  return true;
}
