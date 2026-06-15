#include "service/QueueBuilder.hpp"

#include "repo/EntryRepo.hpp"
#include "repo/QueueRepo.hpp"
#include "repo/SyncRepo.hpp"

#include <filesystem>
#include <optional>

QueueBuilder::QueueBuilder(EntryRepo &entryRepo, QueueRepo &queueRepo,
                           SyncRepo &syncRepo)
    : entryRepo_(entryRepo), queueRepo_(queueRepo), syncRepo_(syncRepo) {}

void QueueBuilder::build(const std::string &syncRootId) {
  const auto syncRoot = syncRepo_.findSyncRootById(syncRootId);
  if (!syncRoot.has_value()) {
    return;
  }

  for (const auto &entry : entryRepo_.listPendingUploadFilesBySyncRootId(syncRootId)) {
    if (queueRepo_.hasActiveUploadFileForEntry(entry.id) || !entry.size.has_value() ||
        *entry.size < 0) {
      continue;
    }

    std::optional<std::string> remoteFolderId;
    const std::filesystem::path parentPath =
        std::filesystem::path(entry.relativePath).parent_path();
    if (parentPath.empty()) {
      if (!syncRoot->folderId.empty()) {
        remoteFolderId = syncRoot->folderId;
      }
    } else {
      const auto parentEntry =
          entryRepo_.findEntryByPath(syncRootId, parentPath.generic_string());
      if (!parentEntry.has_value() || !parentEntry->remoteId.has_value()) {
        continue;
      }

      remoteFolderId = parentEntry->remoteId;
    }

    queueRepo_.enqueueUploadFile(entry.id, entry.relativePath, remoteFolderId,
                                 static_cast<uint64_t>(*entry.size));
  }
}
