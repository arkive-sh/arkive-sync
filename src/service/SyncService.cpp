#include "service/SyncService.hpp"
#include "fs/FileHasher.hpp"
#include "fs/FileScanner.hpp"
#include "helpers/PathCodec.hpp"

#include <chrono>
#include <filesystem>
#include <optional>
#include <random>
#include <spdlog/spdlog.h>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace {

constexpr size_t kScanUpsertBatchSize = 500;

std::string generateId() {
  static std::mt19937_64 rng(std::random_device{}());
  static std::uniform_int_distribution<uint64_t> dist;

  std::ostringstream stream;
  stream << std::hex << dist(rng) << dist(rng);
  return stream.str();
}

std::string toMtimeString(const std::filesystem::file_time_type &time) {
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      time.time_since_epoch())
                      .count();
  return std::to_string(static_cast<long long>(ms));
}

bool shouldMarkPendingUpload(const std::optional<EntryRecord> &existingEntry,
                             const LocalEntry &scannedEntry) {
  if (!existingEntry.has_value()) {
    return true;
  }

  if (existingEntry->remoteDeletedAt.has_value()) {
    // Preserve remote tombstone.
    // Do not enqueue upload.
    return false;
  }

  if (existingEntry->isDirectory != scannedEntry.isDirectory) {
    return true;
  }
  return false;
}

std::optional<EntryRecord>
toEntryRecord(const std::optional<EntryIdentity> &existingEntry,
              const std::string &syncRootId, const std::string &localPath) {
  if (!existingEntry.has_value()) {
    return std::nullopt;
  }

  return EntryRecord{
      .id = existingEntry->id,
      .remoteId = existingEntry->remoteId,
      .remoteFileId = existingEntry->remoteFileId,
      .remoteFolderId = existingEntry->remoteFolderId,
      .syncRootId = syncRootId,
      .remoteType = existingEntry->isDirectory ? "folder" : "file",
      .localPath = localPath,
      .isDirectory = existingEntry->isDirectory,
      .parentFolderId = existingEntry->parentFolderId,
      .remoteParentFolderId = existingEntry->remoteParentFolderId,
      .encryptedName = existingEntry->encryptedName,
      .localSize = existingEntry->localSize,
      .localMtime = existingEntry->localMtime,
      .localHash = existingEntry->localHash,
      .remoteUpdatedAt = existingEntry->remoteUpdatedAt,
      .remoteDeletedAt = existingEntry->remoteDeletedAt,
      .remotePurgedAt = existingEntry->remotePurgedAt,
      .lastRemoteSeenAt = existingEntry->lastRemoteSeenAt,
      .syncState = existingEntry->syncState,
      .lastSyncedAt = existingEntry->lastSyncedAt,
  };
}

bool isUnchanged(const EntryIdentity &existingEntry, bool isDirectory,
                 const std::optional<int64_t> &localSize,
                 const std::string &localMtime) {
  if (existingEntry.syncState == "deleted") {
    return false;
  }
  if (existingEntry.isDirectory != isDirectory) {
    return false;
  }
  if (existingEntry.localSize != localSize) {
    return false;
  }
  return existingEntry.localMtime == localMtime;
}

std::filesystem::path normalizeAbsolute(const std::filesystem::path &path) {
  return std::filesystem::absolute(path).lexically_normal();
}

bool isInsideRoot(const std::filesystem::path &rootPath,
                  const std::filesystem::path &path) {
  const std::filesystem::path relative = path.lexically_relative(rootPath);
  return !relative.empty() && !relative.is_absolute() &&
         *relative.begin() != "..";
}

std::optional<LocalEntry>
readLocalEntry(const std::filesystem::path &rootPath,
               const std::filesystem::path &absolutePath) {
  std::error_code error;
  const std::filesystem::file_status status =
      std::filesystem::symlink_status(absolutePath, error);
  if (error) {
    return std::nullopt;
  }

  const bool isDirectory = std::filesystem::is_directory(status);
  const bool isRegularFile = std::filesystem::is_regular_file(status);
  if (!isDirectory && !isRegularFile) {
    return std::nullopt;
  }

  const uint64_t size =
      isRegularFile ? std::filesystem::file_size(absolutePath, error) : 0;
  if (error) {
    return std::nullopt;
  }

  const auto modifiedTime =
      std::filesystem::last_write_time(absolutePath, error);
  if (error) {
    return std::nullopt;
  }

  return LocalEntry{
      .absolutePath = normalizeAbsolute(absolutePath),
      .relativePath = absolutePath.lexically_relative(rootPath),
      .size = size,
      .modifiedTime = modifiedTime,
      .isDirectory = isDirectory,
  };
}

std::optional<EntryUpsertRecord>
buildEntryUpsertRecord(SyncRepo &syncRepo, RustCrypto &crypto,
                       const std::string &syncRootId, const LocalEntry &entry,
                       const SyncScanSession &scanSession) {
  const std::string relativePath = PathCodec::toDbRelative(entry.relativePath);
  const std::string localPathHash =
      syncRepo.local().computeLocalPathHash(relativePath);
  const std::string localMtime = toMtimeString(entry.modifiedTime);
  const std::optional<EntryIdentity> existingScanState =
      scanSession.findEntryIdentityByPathHash(syncRootId, localPathHash);

  const std::optional<EntryRecord> existingEntry =
      toEntryRecord(existingScanState, syncRootId, relativePath);

  const bool remoteDeletePending = existingEntry.has_value() &&
                                   existingEntry->remoteDeletedAt.has_value() &&
                                   existingEntry->syncState != "deleted";

  if (remoteDeletePending) {
    return std::nullopt;
  }

  const std::optional<int64_t> localSize =
      entry.isDirectory
          ? std::nullopt
          : std::optional<int64_t>(static_cast<int64_t>(entry.size));

  if (existingScanState.has_value() &&
      isUnchanged(*existingScanState, entry.isDirectory, localSize,
                  localMtime)) {
    return std::nullopt;
  }

  std::optional<std::string> localHash =
      existingEntry.has_value() ? existingEntry->localHash : std::nullopt;
  std::string syncState =
      existingEntry.has_value() ? existingEntry->syncState : "pending_upload";

  const bool needsHash =
      !entry.isDirectory &&
      (!existingEntry.has_value() || existingEntry->localSize != localSize ||
       existingEntry->localMtime != localMtime ||
       existingEntry->localHash == std::nullopt ||
       existingEntry->syncState == "deleted");

  if (needsHash) {
    FileHasher hasher(entry.absolutePath, crypto);
    localHash = hasher.hashFile();

    if (!existingEntry.has_value() || existingEntry->syncState == "deleted" ||
        existingEntry->localHash != localHash) {
      syncState = "pending_upload";
    }
  } else if (shouldMarkPendingUpload(existingEntry, entry)) {
    syncState = "pending_upload";
  }

  return EntryUpsertRecord{
      .entry =
          EntryRecord{
              .id =
                  existingEntry.has_value() ? existingEntry->id : generateId(),
              .remoteId = existingEntry.has_value() ? existingEntry->remoteId
                                                    : std::nullopt,
              .syncRootId = syncRootId,
              .remoteType = entry.isDirectory ? "folder" : "file",
              .localPath = relativePath,
              .isDirectory = entry.isDirectory,
              .parentFolderId = existingEntry.has_value()
                                    ? existingEntry->parentFolderId
                                    : std::nullopt,
              .encryptedName = existingEntry.has_value()
                                   ? existingEntry->encryptedName
                                   : std::nullopt,
              .localSize = localSize,
              .localMtime = localMtime,
              .localHash = localHash,
              .remoteUpdatedAt = existingEntry.has_value()
                                     ? existingEntry->remoteUpdatedAt
                                     : std::nullopt,
              .syncState = syncState,
              .lastSyncedAt = existingEntry.has_value()
                                  ? existingEntry->lastSyncedAt
                                  : std::nullopt,
          },
      .localPathHash = localPathHash,
  };
}

size_t flushPendingEntries(SyncRepo &syncRepo,
                           std::vector<EntryUpsertRecord> &entryRecords) {
  if (entryRecords.empty()) {
    return 0;
  }

  const size_t changedCount =
      syncRepo.local().upsertScannedEntries(entryRecords);
  entryRecords.clear();
  return changedCount;
}

class ScanMemoryGuard {
public:
  explicit ScanMemoryGuard(SyncRepo &syncRepo) : syncRepo_(syncRepo) {}
  ~ScanMemoryGuard() { syncRepo_.releaseMemory(); }

  ScanMemoryGuard(const ScanMemoryGuard &) = delete;
  ScanMemoryGuard &operator=(const ScanMemoryGuard &) = delete;

private:
  SyncRepo &syncRepo_;
};

template <typename Callback>
void visitDirectorySubtree(const std::filesystem::path &rootPath,
                           const std::filesystem::path &directoryPath,
                           bool includeDirectory, Callback &&onEntry) {
  if (includeDirectory) {
    if (const auto entry = readLocalEntry(rootPath, directoryPath);
        entry.has_value()) {
      onEntry(*entry);
    }
  }

  std::error_code iteratorError;
  for (const auto &entry : std::filesystem::recursive_directory_iterator(
           directoryPath,
           std::filesystem::directory_options::skip_permission_denied,
           iteratorError)) {
    if (iteratorError) {
      break;
    }

    if (const auto localEntry = readLocalEntry(rootPath, entry.path());
        localEntry.has_value()) {
      onEntry(*localEntry);
    }
  }
}

} // namespace

SyncService::SyncService(SyncRepo &syncRepo, RustCrypto &crypto)
    : syncRepo_(syncRepo), crypto_(crypto) {}

void SyncService::addPath(const std::filesystem::path &rootPathInput) {
  FileScanner fileScanner(rootPathInput);
  const std::filesystem::path rootPath =
      normalizeAbsolute(fileScanner.rootPath());
  const std::string rootPathString = rootPath.string();
  auto syncRoot = syncRepo_.roots().getSyncRootByLocalPath(rootPathString);
  if (!syncRoot.has_value()) {
    syncRoot = SyncRootRecord{
        .id = generateId(),
        .localPath = rootPathString,
        .folderId = std::nullopt,
        .enabled = true,
    };
  }

  syncRepo_.roots().upsertSyncRoot(*syncRoot);
}

size_t SyncService::scanPath(const std::string &rootId,
                             const std::filesystem::path &absolutePathInput) {
  spdlog::info("SyncService scanPath start root={} path={}", rootId,
               absolutePathInput.string());
  const auto syncRoot = syncRepo_.roots().getSyncRootById(rootId);
  if (!syncRoot.has_value()) {
    spdlog::info("SyncService scanPath skipped missing root={}", rootId);
    return 0;
  }

  const std::filesystem::path rootPath = normalizeAbsolute(syncRoot->localPath);
  const std::filesystem::path absolutePath =
      normalizeAbsolute(absolutePathInput);

  if (!isInsideRoot(rootPath, absolutePath) && absolutePath != rootPath) {
    spdlog::info("SyncService scanPath skipped outside root path={}",
                 absolutePath.string());
    return 0;
  }

  std::error_code statusError;
  const bool pathExists = std::filesystem::exists(absolutePath, statusError);
  if (statusError) {
    spdlog::warn("Skipping path scan for {}: {}", absolutePath.string(),
                 statusError.message());
    return 0;
  }

  if (!pathExists) {
    size_t changedCount = 0;
    if (absolutePath == rootPath) {
      changedCount = syncRepo_.local().markSubtreeDeleted(rootId, "");
      spdlog::info("SyncService scanPath root missing root={} changed={}",
                   rootId, changedCount);
      return changedCount;
    }

    const std::filesystem::path relativePath =
        absolutePath.lexically_relative(rootPath);
    if (relativePath.empty() || relativePath == ".") {
      spdlog::info("SyncService scanPath skipped empty relative path root={}",
                   rootId);
      return 0;
    }

    const std::string dbRelativePath = PathCodec::toDbRelative(relativePath);
    const auto scanSession = syncRepo_.beginScan();
    ScanMemoryGuard guard(syncRepo_);
    const std::optional<EntryIdentity> existingEntry =
        scanSession.findEntryIdentityByPathHash(
            rootId, syncRepo_.local().computeLocalPathHash(dbRelativePath));

    if (existingEntry.has_value() && existingEntry->isDirectory) {
      changedCount =
          syncRepo_.local().markSubtreeDeleted(rootId, dbRelativePath);
    } else {
      changedCount = syncRepo_.local().markPathDeleted(rootId, dbRelativePath);
    }
    spdlog::info(
        "SyncService scanPath missing target root={} path={} changed={}",
        rootId, absolutePath.string(), changedCount);
    return changedCount;
  }

  if (std::filesystem::is_regular_file(absolutePath, statusError)) {
    if (statusError) {
      spdlog::warn("Skipping path scan for {}: {}", absolutePath.string(),
                   statusError.message());
      return 0;
    }

    const auto scanSession = syncRepo_.beginScan();
    ScanMemoryGuard guard(syncRepo_);
    const auto localEntry = readLocalEntry(rootPath, absolutePath);
    if (!localEntry.has_value()) {
      return 0;
    }

    std::vector<EntryUpsertRecord> entryRecords;
    if (auto entry = buildEntryUpsertRecord(syncRepo_, crypto_, rootId,
                                            *localEntry, scanSession);
        entry.has_value()) {
      entryRecords.push_back(std::move(*entry));
    }

    const size_t changedCount = flushPendingEntries(syncRepo_, entryRecords);
    spdlog::info("SyncService scanPath file done root={} path={} changed={}",
                 rootId, absolutePath.string(), changedCount);
    return changedCount;
  }

  if (std::filesystem::is_directory(absolutePath, statusError)) {
    if (statusError) {
      spdlog::warn("Skipping path scan for {}: {}", absolutePath.string(),
                   statusError.message());
      return 0;
    }

    const auto scanSession = syncRepo_.beginScan();
    ScanMemoryGuard guard(syncRepo_);
    std::vector<EntryUpsertRecord> entryRecords;
    entryRecords.reserve(kScanUpsertBatchSize);
    size_t changedCount = 0;
    const std::string dbRelativePath =
        absolutePath == rootPath
            ? std::string()
            : PathCodec::toDbRelative(
                  absolutePath.lexically_relative(rootPath));

    visitDirectorySubtree(
        rootPath, absolutePath, absolutePath != rootPath,
        [&](const LocalEntry &entry) {
          scanSession.recordSeenPath(syncRepo_.local().computeLocalPathHash(
              PathCodec::toDbRelative(entry.relativePath)));
          if (auto upsert = buildEntryUpsertRecord(syncRepo_, crypto_, rootId,
                                                   entry, scanSession);
              upsert.has_value()) {
            entryRecords.push_back(std::move(*upsert));
          }

          if (entryRecords.size() >= kScanUpsertBatchSize) {
            changedCount += flushPendingEntries(syncRepo_, entryRecords);
          }
        });

    changedCount += flushPendingEntries(syncRepo_, entryRecords);
    changedCount += syncRepo_.local().markMissingEntriesDeletedUnderPrefix(
        rootId, dbRelativePath);
    spdlog::info(
        "SyncService scanPath directory done root={} path={} changed={}",
        rootId, absolutePath.string(), changedCount);
    return changedCount;
  }

  spdlog::info("SyncService scanPath skipped unsupported path={}",
               absolutePath.string());
  return 0;
}

size_t SyncService::scanRoot(const std::filesystem::path &rootPathInput) {
  const std::filesystem::path rootPath = normalizeAbsolute(rootPathInput);
  const std::string rootPathString = rootPath.string();
  spdlog::info("SyncService scanRoot start path={}", rootPathString);
  auto syncRoot = syncRepo_.roots().getSyncRootByLocalPath(rootPathString);
  if (!syncRoot.has_value()) {
    syncRoot = SyncRootRecord{
        .id = generateId(),
        .localPath = rootPathString,
        .folderId = std::nullopt,
        .enabled = true,
    };
    syncRepo_.roots().upsertSyncRoot(*syncRoot);
  }

  std::error_code statusError;
  const bool rootExists = std::filesystem::exists(rootPath, statusError);
  if (statusError) {
    spdlog::warn("Skipping scan for {}: {}", rootPathString,
                 statusError.message());
    return 0;
  }

  if (!rootExists) {
    auto scanSession = syncRepo_.beginScan();
    ScanMemoryGuard guard(syncRepo_);
    const size_t changedCount =
        syncRepo_.local().markMissingEntriesDeletedForCurrentScan(syncRoot->id);
    spdlog::warn("Sync root missing during scan: {}", rootPathString);
    spdlog::info("SyncService scanRoot missing path={} changed={}",
                 rootPathString, changedCount);
    return changedCount;
  }

  if (!std::filesystem::is_directory(rootPath, statusError)) {
    if (statusError) {
      spdlog::warn("Skipping scan for {}: {}", rootPathString,
                   statusError.message());
    } else {
      spdlog::warn("Skipping scan for {}: path is not a directory",
                   rootPathString);
    }
    return 0;
  }

  FileScanner fileScanner(rootPath);
  auto scanSession = syncRepo_.beginScan();
  ScanMemoryGuard guard(syncRepo_);
  std::vector<EntryUpsertRecord> entryRecords;
  entryRecords.reserve(kScanUpsertBatchSize);
  size_t changedCount = 0;

  try {
    fileScanner.scanFiles([&](const LocalEntry &entry) {
      const std::string relativePath =
          PathCodec::toDbRelative(entry.relativePath);
      scanSession.recordSeenPath(
          syncRepo_.local().computeLocalPathHash(relativePath));

      if (auto upsert = buildEntryUpsertRecord(syncRepo_, crypto_, syncRoot->id,
                                               entry, scanSession);
          upsert.has_value()) {
        entryRecords.push_back(std::move(*upsert));
      }

      if (entryRecords.size() >= kScanUpsertBatchSize) {
        changedCount += flushPendingEntries(syncRepo_, entryRecords);
      }
    });
  } catch (const std::filesystem::filesystem_error &error) {
    spdlog::warn("Skipping scan for {}: {}", rootPathString, error.what());
    return 0;
  } catch (const std::system_error &error) {
    spdlog::warn("Skipping scan for {}: {}", rootPathString, error.what());
    return 0;
  }

  changedCount += flushPendingEntries(syncRepo_, entryRecords);
  changedCount +=
      syncRepo_.local().markMissingEntriesDeletedForCurrentScan(syncRoot->id);
  spdlog::info("SyncService scanRoot done path={} changed={}", rootPathString,
               changedCount);

  return changedCount;
}
