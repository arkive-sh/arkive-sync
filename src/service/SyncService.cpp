#include "service/SyncService.hpp"

#include <chrono>
#include <filesystem>
#include <optional>
#include <random>
#include <sstream>
#include <string>
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
  return std::to_string(time.time_since_epoch().count());
}

} // namespace

SyncService::SyncService(SyncRepo &syncRepo, FileScanner &fileScanner)
    : syncRepo_(syncRepo), fileScanner_(fileScanner) {}

size_t SyncService::addPath() {
  const auto scannedEntries = fileScanner_.scanFiles();
  const std::filesystem::path rootPath =
      std::filesystem::absolute(fileScanner_.rootPath());

  syncRepo_.upsertSyncRoot(SyncRootRecord{
      .id = generateId(),
      .localPath = rootPath.string(),
      .folderId = std::nullopt,
      .enabled = true,
  });

  std::vector<EntryRecord> entryRecords;
  entryRecords.reserve(scannedEntries.size());

  for (const auto &entry : scannedEntries) {
    entryRecords.push_back(EntryRecord{
        .id = generateId(),
        .remoteId = std::nullopt,
        .remoteType = entry.isDirectory ? "folder" : "file",
        .localPath = entry.absolutePath.string(),
        .parentFolderId = std::nullopt,
        .encryptedName = std::nullopt,
        .localSize = entry.isDirectory ? std::nullopt
                                       : std::optional<int64_t>(
                                             static_cast<int64_t>(entry.size)),
        .localMtime = toMtimeString(entry.modifiedTime),
        .localHash = std::nullopt,
        .remoteUpdatedAt = std::nullopt,
        .syncState = "pending_upload",
        .lastSyncedAt = std::nullopt,
    });
  }

  syncRepo_.upsertEntries(entryRecords);
  return entryRecords.size();
}
