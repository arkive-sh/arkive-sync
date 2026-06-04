#include "service/SyncService.hpp"
#include "fs/FileScanner.hpp"
#include "fs/FileHasher.hpp"
#include "helpers/PathCodec.hpp"

#include <chrono>
#include <filesystem>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr size_t kScanUpsertBatchSize = 1000;

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

  if (existingEntry->syncState == "deleted") {
    return true;
  }

  if (existingEntry->isDirectory != scannedEntry.isDirectory) {
    return true;
  }
  return false;
}

} // namespace

SyncService::SyncService(SyncRepo &syncRepo, QueueRepo &queueRepo,
                         RustCrypto &crypto)
    : syncRepo_(syncRepo), queueRepo_(queueRepo), crypto_(crypto) {}

void SyncService::addPath(const std::filesystem::path &rootPathInput) {
  FileScanner fileScanner(rootPathInput);
  const std::filesystem::path rootPath =
      std::filesystem::absolute(fileScanner.rootPath()).lexically_normal();
  const std::string rootPathString = rootPath.string();
  auto syncRoot = syncRepo_.getSyncRootByLocalPath(rootPathString);
  if (!syncRoot.has_value()) {
    syncRoot = SyncRootRecord{
        .id = generateId(),
        .localPath = rootPathString,
        .folderId = std::nullopt,
        .enabled = true,
    };
  }

  syncRepo_.upsertSyncRoot(*syncRoot);
}

size_t SyncService::scanRoot(const std::filesystem::path &rootPathInput) {
  FileScanner fileScanner(rootPathInput);
  const std::filesystem::path rootPath =
      std::filesystem::absolute(fileScanner.rootPath()).lexically_normal();
  const std::string rootPathString = rootPath.string();
  auto syncRoot = syncRepo_.getSyncRootByLocalPath(rootPathString);
  if (!syncRoot.has_value()) {
    syncRoot = SyncRootRecord{
        .id = generateId(),
        .localPath = rootPathString,
        .folderId = std::nullopt,
        .enabled = true,
    };
    syncRepo_.upsertSyncRoot(*syncRoot);
  }

  syncRepo_.markEntriesUnseen(syncRoot->id);
  std::vector<EntryRecord> entryRecords;
  entryRecords.reserve(kScanUpsertBatchSize);
  size_t scannedCount = 0;

  fileScanner.scanFiles([&](const LocalEntry &entry) {
    const std::string relativePath = PathCodec::toDbRelative(entry.relativePath);
    const std::string localMtime = toMtimeString(entry.modifiedTime);
    const std::optional<EntryRecord> existingEntry =
        syncRepo_.getEntryByLocalPath(syncRoot->id, relativePath);

    const std::optional<int64_t> localSize =
        entry.isDirectory
            ? std::nullopt
            : std::optional<int64_t>(static_cast<int64_t>(entry.size));

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
      FileHasher hasher(entry.absolutePath, crypto_);
      localHash = hasher.hashFile();

      if (!existingEntry.has_value() || existingEntry->syncState == "deleted" ||
          existingEntry->localHash != localHash) {
        syncState = "pending_upload";
      }
    } else if (shouldMarkPendingUpload(existingEntry, entry)) {
      syncState = "pending_upload";
    }

    entryRecords.push_back(EntryRecord{
        .id = existingEntry.has_value() ? existingEntry->id : generateId(),
        .remoteId =
            existingEntry.has_value() ? existingEntry->remoteId : std::nullopt,
        .syncRootId = syncRoot->id,
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
        .lastSyncedAt = existingEntry.has_value() ? existingEntry->lastSyncedAt
                                                  : std::nullopt,
    });
    ++scannedCount;

    if (entryRecords.size() >= kScanUpsertBatchSize) {
      syncRepo_.upsertEntries(entryRecords);
      entryRecords.clear();
    }
  });

  if (!entryRecords.empty()) {
    syncRepo_.upsertEntries(entryRecords);
  }
  syncRepo_.markUnseenEntriesDeleted(syncRoot->id);

  return scannedCount;
}
