#include "service/UploadJobRunner.hpp"

#include "crypto/RustCrypto.hpp"
#include "fs/FileHasher.hpp"
#include "helpers/PathCodec.hpp"
#include "repo/SyncRepo.hpp"
#include "service/UploadService.hpp"
#include <filesystem>
#include <stdexcept>

UploadJobRunner::UploadJobRunner(SyncRepo &syncRepo,
                                 UploadService &uploadService,
                                 RustCrypto &crypto)
    : syncRepo_(syncRepo), uploadService_(uploadService), crypto_(crypto) {}

void UploadJobRunner::run(const TransferJob &job) {
  const auto entry = syncRepo_.getEntryById(job.entryId);
  if (!entry.has_value()) {
    throw std::runtime_error("Queued upload entry is missing");
  }

  const auto syncRoot = syncRepo_.getSyncRootById(entry->syncRootId);
  if (!syncRoot.has_value()) {
    throw std::runtime_error("Queued upload sync root is missing");
  }

  const std::filesystem::path absolutePath =
      PathCodec::joinRoot(syncRoot->localPath, entry->localPath);

  if (!std::filesystem::exists(absolutePath)) {
    throw std::runtime_error("Queued upload file is missing");
  }
  if (!std::filesystem::is_regular_file(absolutePath)) {
    throw std::runtime_error("Queued upload path is not a regular file");
  }

  if (entry->localHash.has_value()) {
    FileHasher hasher(absolutePath, crypto_);
    const std::string currentHash = hasher.hashFile();
    if (currentHash != *entry->localHash) {
      throw std::runtime_error(
          "Queued upload file contents changed since the last sync scan");
    }
  }

  uploadService_.uploadFile(absolutePath, *entry);
}
