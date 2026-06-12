#include "./RootScanner.hpp"
#include "fs/FileHasher.hpp"
#include "fs/FileWatcher.hpp"
#include "helpers/GenUUID.hpp"
#include "repo/ScanRepo.hpp"
#include "repo/SyncRepo.hpp"
#include "service/SyncService.hpp"
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>

static constexpr int kMaxScanBatchSize = 500;

RootScanner::RootScanner(RustCrypto &crypto, SyncService &syncSvc,
                         ScanRepo &scanRepo, DirtyPathRepo &dirtyPathRepo,
                         EntryRepo &entryRepo)
    : watcher_(IFileWatcher::create()), crypto_(crypto), syncSvc_(syncSvc),
      scanRepo_(scanRepo), dirtyPathRepo_(dirtyPathRepo),
      entryRepo_(entryRepo) {}

RootScanner::RootScanner(RustCrypto &crypto, SyncService &syncSvc,
                         ScanRepo &scanRepo, DirtyPathRepo &dirtyPathRepo,
                         EntryRepo &entryRepo,
                         std::unique_ptr<IFileWatcher> watcher)
    : watcher_(std::move(watcher)), crypto_(crypto), syncSvc_(syncSvc),
      scanRepo_(scanRepo), dirtyPathRepo_(dirtyPathRepo),
      entryRepo_(entryRepo) {}

// Helper to create a iterator
auto makeIterator = [](const std::filesystem::path &path,
                       std::error_code &iterEc) {
  return std::filesystem::recursive_directory_iterator(
      path, std::filesystem::directory_options::skip_permission_denied, iterEc);
};

bool positionAfterCursor(ScanJob &job, std::error_code &ec,
                         std::filesystem::recursive_directory_iterator &it,
                         std::filesystem::recursive_directory_iterator &end,
                         std::filesystem::path &rootPath) {
  bool cursorMatched = false;

  while (!ec && it != end) {
    std::filesystem::path currentRelPath =
        std::filesystem::relative(it->path(), rootPath, ec);

    if (ec)
      break;

    if (currentRelPath.generic_string() == *job.cursorPath) {
      cursorMatched = true;
      break;
    }

    it.increment(ec);
  }

  job.cursorPath = std::nullopt;
  it = makeIterator(rootPath, ec);
  return !ec;
}

bool RootScanner::handleFileEntry(const std::string &syncRootId,
                                  const ScanJob &job,
                                  const std::filesystem::path &absPath,
                                  const std::string &relativePath,
                                  std::error_code &ec) {
  const auto fileSize = std::filesystem::file_size(absPath, ec);
  if (ec) {
    return false;
  }

  if (fileSize >
      static_cast<uintmax_t>(std::numeric_limits<int64_t>::max())) {
    return false;
  }

  const auto mtime = std::filesystem::last_write_time(absPath, ec);
  if (ec) {
    return false;
  }

  const auto existing = entryRepo_.findEntryByPath(syncRootId, relativePath);

  std::optional<std::string> contentHash;
  bool shouldHash = true;

  if (existing && !existing->deleted && existing->size == fileSize &&
      existing->mtime == mtime) {
    shouldHash = false;
    contentHash = existing->contentHash;
  }

  if (shouldHash) {
    contentHash = FileHasher(absPath, crypto_).hashFile();
    if (!contentHash.has_value()) {
      return false;
    }
  }

  std::string syncState = "unchanged";

  if (!existing || existing->deleted) {
    syncState = "pending_upload";
  } else if (shouldHash && existing->contentHash != contentHash) {
    syncState = "pending_upload";
  }

  entryRepo_.upsertFileEntry({
      .syncRootId = syncRootId,
      .relativePath = relativePath,
      .size = static_cast<int64_t>(fileSize),
      .mtime = mtime,
      .contentHash = *contentHash,
      .syncState = syncState,
      .lastSeenScanId = job.id,
  });

  return true;
}

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

  std::filesystem::recursive_directory_iterator it = makeIterator(rootPath, ec);
  if (ec) {
    return false;
  }

  std::filesystem::recursive_directory_iterator end;
  bool positioned = positionAfterCursor(job, ec, it, end, rootPath);
  if (!positioned) {
    return false;
  }

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

    if (!positioned) {
      if (rel == *job.cursorPath) {
        positioned = true;
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
      entryRepo_.upsertDirectoryEntry({
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
      if (!handleFileEntry(syncRootId, job, absPath, rel, ec)) {
        it.increment(ec);
        continue;
      }

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
    entryRepo_.markEntriesNotSeenInScanDeleted(syncRootId, job.id);
    return true;
  }

  if (lastCursor.has_value()) {
    scanRepo_.updateScanCursor(job.id, *lastCursor);
  }

  return true;
}
