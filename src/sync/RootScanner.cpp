#include "./RootScanner.hpp"
#include "db/SqliteHelpers.hpp"
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
#include <spdlog/spdlog.h>

static constexpr int kMaxScanBatchSize = 500;

RootScanner::RootScanner(sqlite3 *db, RustCrypto &crypto, SyncService &syncSvc,
                         ScanRepo &scanRepo, DirtyPathRepo &dirtyPathRepo,
                         EntryRepo &entryRepo)
    : watcher_(IFileWatcher::create()), db_(db), crypto_(crypto),
      syncSvc_(syncSvc), scanRepo_(scanRepo), dirtyPathRepo_(dirtyPathRepo),
      entryRepo_(entryRepo) {}

RootScanner::RootScanner(sqlite3 *db, RustCrypto &crypto, SyncService &syncSvc,
                         ScanRepo &scanRepo, DirtyPathRepo &dirtyPathRepo,
                         EntryRepo &entryRepo,
                         std::unique_ptr<IFileWatcher> watcher)
    : watcher_(std::move(watcher)), db_(db), crypto_(crypto),
      syncSvc_(syncSvc), scanRepo_(scanRepo), dirtyPathRepo_(dirtyPathRepo),
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
  if (!job.cursorPath.has_value()) {
    return true;
  }

  while (!ec && it != end) {
    std::filesystem::path currentRelPath =
        std::filesystem::relative(it->path(), rootPath, ec);

    if (ec)
      break;

    if (currentRelPath.generic_string() == *job.cursorPath) {
      it.increment(ec);
      break;
    }

    it.increment(ec);
  }

  if (ec) {
    return false;
  }

  if (it == end) {
    spdlog::warn("Scan cursor not found for root {} at {}, restarting from root",
                 job.syncRootId, *job.cursorPath);
    job.cursorPath = std::nullopt;
    it = makeIterator(rootPath, ec);
  } else {
    spdlog::info("Resuming scan for root {} after cursor {}", job.syncRootId,
                 *job.cursorPath);
  }

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

  EntrySyncState syncState = EntrySyncState::Unchanged;

  if (!existing || existing->deleted) {
    syncState = EntrySyncState::PendingUpload;
  } else if (shouldHash && existing->contentHash != contentHash) {
    syncState = EntrySyncState::PendingUpload;
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
  execOrThrow(db_, "BEGIN IMMEDIATE;");
  try {
    if (scanJob) {
      job = *scanJob;
      if (job.cursorPath.has_value()) {
        spdlog::info("Starting scan batch for root {} from cursor {}", syncRootId,
                     *job.cursorPath);
      } else {
        spdlog::info("Starting scan batch for root {} from root", syncRootId);
      }
    } else {
      job = ScanJob{
          .id = generateUUID(),
          .syncRootId = syncRootId,
          .status = "running",
          .cursorPath = std::nullopt,
      };

      if (!scanRepo_.insertScanJob(job)) {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
      }

      spdlog::info("Created new scan job {} for root {}", job.id, syncRootId);
    }

    std::filesystem::recursive_directory_iterator it = makeIterator(rootPath, ec);
    if (ec) {
      sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
      return false;
    }

    std::filesystem::recursive_directory_iterator end;
    if (!positionAfterCursor(job, ec, it, end, rootPath)) {
      sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
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
      execOrThrow(db_, "COMMIT;");
      spdlog::info("Completed scan job {} for root {}", job.id, syncRootId);
      return true;
    }

    if (lastCursor.has_value()) {
      scanRepo_.updateScanCursor(job.id, *lastCursor);
      spdlog::info("Checkpointed scan job {} for root {} at {}", job.id,
                   syncRootId, *lastCursor);
    }

    execOrThrow(db_, "COMMIT;");
    return true;
  } catch (...) {
    sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
    throw;
  }
}
