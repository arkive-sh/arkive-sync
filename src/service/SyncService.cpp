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
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

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
  // 1. Resolve sync root path and make sure root record exists.
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

  // 2. Load current DB view for this root.
  const auto existingEntries = syncRepo_.getEntriesForSyncRoot(syncRoot->id);
  std::unordered_map<std::string, EntryRecord> existingEntriesByPath;
  existingEntriesByPath.reserve(existingEntries.size());
  for (const auto &entry : existingEntries) {
    existingEntriesByPath.emplace(entry.localPath, entry);
  }

  // 3. Scan filesystem and process each unique relative path once.
  const auto scannedEntries = fileScanner.scanFiles();

  std::vector<EntryRecord> entryRecords;
  entryRecords.reserve(scannedEntries.size());

  std::unordered_set<std::string> presentPaths;
  presentPaths.reserve(scannedEntries.size());

  for (const auto &entry : scannedEntries) {
    const std::string relativePath = PathCodec::toDbRelative(entry.relativePath);
    if (!presentPaths.insert(relativePath).second) {
      continue;
    }

    const std::string localMtime = toMtimeString(entry.modifiedTime);
    const auto existingEntryIt = existingEntriesByPath.find(relativePath);

    const std::optional<EntryRecord> existingEntry =
        existingEntryIt != existingEntriesByPath.end()
            ? std::optional<EntryRecord>(existingEntryIt->second)
            : std::nullopt;

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
  }

  // 4. Persist current scan and mark missing entries deleted. Queue admission
  // is handled separately so desktop sync can process arbitrarily large trees
  // in server-sized batches instead of flooding transfer_queue.
  syncRepo_.upsertEntries(entryRecords);
  syncRepo_.markMissingEntriesDeleted(
      syncRoot->id,
      std::vector<std::string>(presentPaths.begin(), presentPaths.end()));

  return entryRecords.size();
}
