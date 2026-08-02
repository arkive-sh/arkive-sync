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
                         EntryRepo &entryRepo, LocalEntryRepo &localEntryRepo)
    : watcher_(IFileWatcher::create()), db_(db), crypto_(crypto),
      syncSvc_(syncSvc), scanRepo_(scanRepo), dirtyPathRepo_(dirtyPathRepo),
      entryRepo_(entryRepo), localEntryRepo_(localEntryRepo) {}

RootScanner::RootScanner(sqlite3 *db, RustCrypto &crypto, SyncService &syncSvc,
                         ScanRepo &scanRepo, DirtyPathRepo &dirtyPathRepo,
                         EntryRepo &entryRepo, LocalEntryRepo &localEntryRepo,
                         std::unique_ptr<IFileWatcher> watcher)
    : watcher_(std::move(watcher)), db_(db), crypto_(crypto), syncSvc_(syncSvc),
      scanRepo_(scanRepo), dirtyPathRepo_(dirtyPathRepo),
      entryRepo_(entryRepo), localEntryRepo_(localEntryRepo) {}

// Helper to create a iterator
auto makeIterator = [](const std::filesystem::path &path,
                       std::error_code &iterEc) {
  return std::filesystem::recursive_directory_iterator(
      path, std::filesystem::directory_options::skip_permission_denied, iterEc);
};

bool positionAfterCursor(ScanJob &job, std::error_code &ec,
                         std::filesystem::recursive_directory_iterator &it,
                         std::filesystem::recursive_directory_iterator &end,
                         const std::filesystem::path &rootPath) {
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
    spdlog::warn(
        "Scan cursor not found for root {} at {}, restarting from root",
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

  if (fileSize > static_cast<uintmax_t>(std::numeric_limits<int64_t>::max())) {
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
  } else if (!existing->remoteId.has_value()) {
    syncState = EntrySyncState::PendingUpload;
  } else if (shouldHash && existing->contentHash != contentHash) {
    syncState = EntrySyncState::PendingUpload;
  }

  localEntryRepo_.upsertFileEntry({
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

bool RootScanner::scanSubtree(const std::string &syncRootId, ScanJob job,
                              const std::filesystem::path &rootPath,
                              const std::filesystem::path &subtreePath,
                              const std::string &relativePath,
                              std::error_code &ec) {
  execOrThrow(db_, "BEGIN IMMEDIATE;");
  try {
    localEntryRepo_.upsertDirectoryEntry({
        .syncRootId = syncRootId,
        .relativePath = relativePath,
        .lastSeenScanId = job.id,
    });

    while (true) {
      std::filesystem::recursive_directory_iterator it =
          makeIterator(subtreePath, ec);
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
        const std::filesystem::path childAbsPath = it->path();
        const std::filesystem::path childRelPath =
            std::filesystem::relative(childAbsPath, rootPath, ec);
        if (ec) {
          it.increment(ec);
          continue;
        }

        const auto childStatus = it->symlink_status(ec);
        if (ec) {
          it.increment(ec);
          continue;
        }

        if (std::filesystem::is_symlink(childStatus)) {
          it.disable_recursion_pending();
          it.increment(ec);
          continue;
        }

        if (std::filesystem::is_directory(childStatus)) {
          localEntryRepo_.upsertDirectoryEntry({
              .syncRootId = syncRootId,
              .relativePath = childRelPath.generic_string(),
              .lastSeenScanId = job.id,
          });
          lastCursor = childRelPath.generic_string();
          processed++;
          it.increment(ec);
          continue;
        }

        if (std::filesystem::is_regular_file(childStatus)) {
          if (!handleFileEntry(syncRootId, job, childAbsPath,
                               childRelPath.generic_string(), ec)) {
            it.increment(ec);
            continue;
          }

          lastCursor = childRelPath.generic_string();
          processed++;
          it.increment(ec);
          continue;
        }

        it.disable_recursion_pending();
        it.increment(ec);
      }

      if (ec) {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
      }

      if (it == end) {
        localEntryRepo_.markSubtreeEntriesNotSeenInScanDeleted(
            syncRootId, relativePath, job.id);
        execOrThrow(db_, "COMMIT;");
        return true;
      }

      if (!lastCursor.has_value()) {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
      }

      job.cursorPath = *lastCursor;
    }
  } catch (...) {
    sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
    throw;
  }
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
  execOrThrow(db_, "BEGIN IMMEDIATE;"); // Later move this after file hashing
  try {
    if (scanJob) {
      job = *scanJob;
      if (job.cursorPath.has_value()) {
        spdlog::info("Starting scan batch for root {} from cursor {}",
                     syncRootId, *job.cursorPath);
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

    std::filesystem::recursive_directory_iterator it =
        makeIterator(rootPath, ec);
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
        localEntryRepo_.upsertDirectoryEntry({
            .syncRootId = syncRootId,
            .relativePath = rel,
            .lastSeenScanId = job.id,
        });

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
      localEntryRepo_.markEntriesNotSeenInScanDeleted(syncRootId, job.id);
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

bool RootScanner::scanPath(const std::string &rootId,
                           const std::filesystem::path &relativePath) {
  std::optional<SyncRoot> syncRoot = syncSvc_.findSyncRootById(rootId);
  if (!syncRoot.has_value() || syncRoot->localPath == "") {
    return false;
  }

  std::error_code ec;
  const std::filesystem::path rootPath =
      std::filesystem::absolute(syncRoot->localPath, ec);
  if (ec) {
    return false;
  }

  const std::filesystem::path absPath =
      relativePath.is_absolute() ? relativePath : rootPath / relativePath;
  const std::filesystem::path normalizedAbsPath =
      std::filesystem::absolute(absPath, ec).lexically_normal();
  if (ec) {
    return false;
  }

  const std::filesystem::path normalizedRootPath = rootPath.lexically_normal();
  const std::filesystem::path relPath =
      normalizedAbsPath.lexically_relative(normalizedRootPath);
  if (relPath.empty() || relPath == "." || *relPath.begin() == "..") {
    return false;
  }

  if (!std::filesystem::exists(normalizedAbsPath, ec)) {
    if (ec) {
      return false;
    }

    localEntryRepo_.markPathDeleted(rootId, relPath.generic_string());
    return true;
  }

  const auto status = std::filesystem::symlink_status(normalizedAbsPath, ec);
  if (ec) {
    return false;
  }

  const ScanJob pathJob{
      .id = generateUUID(),
      .syncRootId = rootId,
      .status = "running",
      .cursorPath = std::nullopt,
  };

  if (std::filesystem::is_regular_file(status)) {
    execOrThrow(db_, "BEGIN IMMEDIATE;");
    try {
      const bool ok = handleFileEntry(rootId, pathJob, normalizedAbsPath,
                                      relPath.generic_string(), ec) &&
                      !ec;
      if (!ok) {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
      }

      execOrThrow(db_, "COMMIT;");
      return true;
    } catch (...) {
      sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
      throw;
    }
  }

  if (!std::filesystem::is_directory(status)) {
    return false;
  }

  return scanSubtree(rootId, pathJob, normalizedRootPath, normalizedAbsPath,
                     relPath.generic_string(), ec);
}
