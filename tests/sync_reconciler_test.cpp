#include "db/SqliteHelpers.hpp"
#include "download/DownloadService.hpp"
#include "repo/ConflictRepo.hpp"
#include "repo/EntryRepo.hpp"
#include "repo/LocalEntryRepo.hpp"
#include "repo/RemoteEntryRepo.hpp"
#include "repo/SyncRepo.hpp"
#include "service/SyncReconciler.hpp"

#include "support/TestDatabase.hpp"
#include "support/FakeSecureStorage.hpp"
#include "support/TestFs.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

namespace {

class FakeDownloadService final : public DownloadService {
public:
  FakeDownloadService(ArkiveApi &api, ArkiveHttpClient &http,
                      RustCrypto &crypto,
                      DownloadRecordDecryptor &decryptor)
      : DownloadService(api, http, crypto, decryptor) {}

  void downloadFile(const std::string &fileId,
                    const std::filesystem::path &targetPath) const override {
    lastFileId = fileId;
    lastTargetPath = targetPath;
    writeFile(targetPath, "remote");
  }

  mutable std::optional<std::string> lastFileId;
  mutable std::optional<std::filesystem::path> lastTargetPath;
};

std::string readFile(const std::filesystem::path &path) {
  std::ifstream stream(path, std::ios::binary);
  REQUIRE(stream.is_open());
  return std::string(std::istreambuf_iterator<char>(stream),
                     std::istreambuf_iterator<char>());
}

} // namespace

TEST_CASE("SyncReconciler applies delete local for remote deleted entries") {
  TestDatabase db;
  TempDir tempDir;
  SyncRepo syncRepo(db.get());
  EntryRepo entryRepo(db.get());
  LocalEntryRepo localEntryRepo(db.get());
  RemoteEntryRepo remoteEntryRepo(db.get());
  SyncReconciler reconciler(entryRepo);

  const auto filePath = tempDir.path() / "movie.txt";
  writeFile(filePath, "hello");

  syncRepo.upsertSyncRoot({
      .Id = "root-1",
      .localPath = tempDir.path().string(),
      .folderId = "root-folder-1",
      .enabled = 1,
      .mode = SyncMode::RemoteMirror,
  });

  localEntryRepo.upsertFileEntry({
      .syncRootId = "root-1",
      .relativePath = "movie.txt",
      .size = static_cast<int64_t>(std::filesystem::file_size(filePath)),
      .mtime = std::filesystem::last_write_time(filePath),
      .contentHash = "local-hash-1",
      .syncState = EntrySyncState::Unchanged,
      .lastSeenScanId = "scan-1",
  });

  const auto entry = entryRepo.findEntryByPath("root-1", "movie.txt");
  REQUIRE(entry.has_value());
  remoteEntryRepo.markEntryUploaded(entry->id, "remote-file-1", std::nullopt);
  remoteEntryRepo.upsertRemoteEntry({
      .syncRootId = "root-1",
      .remoteId = "remote-file-1",
      .localPath = "movie.txt",
      .remoteType = "file",
      .remoteFileId = std::optional<std::string>("remote-file-1"),
      .remoteFolderId = std::nullopt,
      .remoteParentFolderId = std::nullopt,
      .encryptedName = std::nullopt,
      .encryptedMetadata = std::nullopt,
      .remoteDeletedAt = std::optional<std::string>("2026-06-19T00:00:00Z"),
      .remoteUpdatedAt = "2026-06-19T00:00:00Z",
  });

  sqlite3_stmt *rawStmt = nullptr;
  REQUIRE(sqlite3_prepare_v2(db.get(),
                             "UPDATE entries SET synced_content_hash = "
                             "local_content_hash, "
                             "synced_remote_updated_at = remote_updated_at "
                             "WHERE id = ?;",
                             -1, &rawStmt, nullptr) == SQLITE_OK);
  StmtUniquePtr stmt(rawStmt);
  bindText(db.get(), stmt.get(), 1, entry->id);
  REQUIRE(sqlite3_step(stmt.get()) == SQLITE_DONE);

  const auto root = syncRepo.findSyncRootById("root-1");
  REQUIRE(root.has_value());

  reconciler.reconcileRoot(*root);

  REQUIRE_FALSE(std::filesystem::exists(filePath));
}

TEST_CASE("SyncReconciler downloads remote files") {
  TestDatabase db;
  TempDir tempDir;
  ArkiveHttpClient http{"http://example.invalid", ""};
  ArkiveApi api{http};
  RustCrypto crypto;
  UserRepo userRepo{db.get()};
  VaultService vaultService{userRepo, crypto,
                            std::make_unique<FakeSecureStorage>()};
  DownloadRecordDecryptor decryptor{crypto, vaultService};
  SyncRepo syncRepo(db.get());
  EntryRepo entryRepo(db.get());
  RemoteEntryRepo remoteEntryRepo(db.get());
  ConflictRepo conflictRepo(db.get());
  FakeDownloadService downloadService(api, http, crypto, decryptor);
  SyncReconciler reconciler(entryRepo, conflictRepo, remoteEntryRepo,
                            &downloadService, &crypto);

  syncRepo.upsertSyncRoot({
      .Id = "root-1",
      .localPath = tempDir.path().string(),
      .folderId = "root-folder-1",
      .enabled = 1,
      .mode = SyncMode::RemoteMirror,
  });

  remoteEntryRepo.upsertRemoteEntry({
      .syncRootId = "root-1",
      .remoteId = "remote-file-1",
      .localPath = "movie.txt",
      .remoteType = "file",
      .remoteFileId = std::optional<std::string>("remote-file-1"),
      .remoteFolderId = std::nullopt,
      .remoteParentFolderId = std::nullopt,
      .encryptedName = std::nullopt,
      .encryptedMetadata = std::nullopt,
      .remoteDeletedAt = std::nullopt,
      .remoteUpdatedAt = "2026-06-19T00:00:00Z",
  });

  const auto root = syncRepo.findSyncRootById("root-1");
  REQUIRE(root.has_value());

  reconciler.reconcileRoot(*root);

  REQUIRE(downloadService.lastFileId ==
          std::optional<std::string>("remote-file-1"));
  REQUIRE(downloadService.lastTargetPath ==
          std::optional<std::filesystem::path>(tempDir.path() / "movie.txt"));
  REQUIRE(std::filesystem::exists(tempDir.path() / "movie.txt"));

  const auto entry = entryRepo.findEntryByPath("root-1", "movie.txt");
  REQUIRE(entry.has_value());
  REQUIRE(entry->syncedRemoteUpdatedAt == entry->remoteUpdatedAt);
  REQUIRE(entry->syncedContentHash == entry->contentHash);
}

TEST_CASE("SyncReconciler keeps local file and downloads remote conflict copy") {
  TestDatabase db;
  TempDir tempDir;
  ArkiveHttpClient http{"http://example.invalid", ""};
  ArkiveApi api{http};
  RustCrypto crypto;
  UserRepo userRepo{db.get()};
  VaultService vaultService{userRepo, crypto,
                            std::make_unique<FakeSecureStorage>()};
  DownloadRecordDecryptor decryptor{crypto, vaultService};
  SyncRepo syncRepo(db.get());
  EntryRepo entryRepo(db.get());
  LocalEntryRepo localEntryRepo(db.get());
  RemoteEntryRepo remoteEntryRepo(db.get());
  ConflictRepo conflictRepo(db.get());
  FakeDownloadService downloadService(api, http, crypto, decryptor);
  SyncReconciler reconciler(entryRepo, conflictRepo, remoteEntryRepo,
                            &downloadService, &crypto);

  const auto filePath = tempDir.path() / "movie.txt";
  writeFile(filePath, "local");

  syncRepo.upsertSyncRoot({
      .Id = "root-1",
      .localPath = tempDir.path().string(),
      .folderId = "root-folder-1",
      .enabled = 1,
      .mode = SyncMode::TwoWay,
  });

  localEntryRepo.upsertFileEntry({
      .syncRootId = "root-1",
      .relativePath = "movie.txt",
      .size = static_cast<int64_t>(std::filesystem::file_size(filePath)),
      .mtime = std::filesystem::last_write_time(filePath),
      .contentHash = "local-new",
      .syncState = EntrySyncState::PendingUpload,
      .lastSeenScanId = "scan-1",
  });

  const auto entry = entryRepo.findEntryByPath("root-1", "movie.txt");
  REQUIRE(entry.has_value());
  remoteEntryRepo.markEntryUploaded(entry->id, "remote-file-1", std::nullopt);

  remoteEntryRepo.upsertRemoteEntry({
      .syncRootId = "root-1",
      .remoteId = "remote-file-1",
      .localPath = "movie.txt",
      .remoteType = "file",
      .remoteFileId = std::optional<std::string>("remote-file-1"),
      .remoteFolderId = std::nullopt,
      .remoteParentFolderId = std::nullopt,
      .encryptedName = std::nullopt,
      .encryptedMetadata = std::nullopt,
      .remoteDeletedAt = std::nullopt,
      .remoteUpdatedAt = "2026-06-19T00:00:00Z",
  });

  sqlite3_stmt *rawStmt = nullptr;
  REQUIRE(sqlite3_prepare_v2(db.get(),
                             "UPDATE entries SET synced_content_hash = "
                             "'local-old', synced_remote_updated_at = "
                             "'2026-06-18T00:00:00Z' WHERE id = ?;",
                             -1, &rawStmt, nullptr) == SQLITE_OK);
  StmtUniquePtr stmt(rawStmt);
  bindText(db.get(), stmt.get(), 1, entry->id);
  REQUIRE(sqlite3_step(stmt.get()) == SQLITE_DONE);

  const auto root = syncRepo.findSyncRootById("root-1");
  REQUIRE(root.has_value());

  reconciler.reconcileRoot(*root);

  REQUIRE(readFile(filePath) == "local");
  REQUIRE(downloadService.lastFileId ==
          std::optional<std::string>("remote-file-1"));
  REQUIRE(downloadService.lastTargetPath.has_value());
  REQUIRE(downloadService.lastTargetPath->filename().string().starts_with(
      "movie (remote conflict "));
  REQUIRE(downloadService.lastTargetPath->extension() == ".txt");
  REQUIRE(readFile(*downloadService.lastTargetPath) == "remote");

  const auto conflicted = entryRepo.findEntryByPath("root-1", "movie.txt");
  REQUIRE(conflicted.has_value());
  REQUIRE(conflicted->conflictState ==
          std::optional<std::string>("local_remote_modified"));
}

TEST_CASE("SyncReconciler does not download remote-deleted conflict copy") {
  TestDatabase db;
  TempDir tempDir;
  ArkiveHttpClient http{"http://example.invalid", ""};
  ArkiveApi api{http};
  RustCrypto crypto;
  UserRepo userRepo{db.get()};
  VaultService vaultService{userRepo, crypto,
                            std::make_unique<FakeSecureStorage>()};
  DownloadRecordDecryptor decryptor{crypto, vaultService};
  SyncRepo syncRepo(db.get());
  EntryRepo entryRepo(db.get());
  LocalEntryRepo localEntryRepo(db.get());
  RemoteEntryRepo remoteEntryRepo(db.get());
  ConflictRepo conflictRepo(db.get());
  FakeDownloadService downloadService(api, http, crypto, decryptor);
  SyncReconciler reconciler(entryRepo, conflictRepo, remoteEntryRepo,
                            &downloadService, &crypto);

  const auto filePath = tempDir.path() / "movie.txt";
  writeFile(filePath, "local");

  syncRepo.upsertSyncRoot({
      .Id = "root-1",
      .localPath = tempDir.path().string(),
      .folderId = "root-folder-1",
      .enabled = 1,
      .mode = SyncMode::TwoWay,
  });

  localEntryRepo.upsertFileEntry({
      .syncRootId = "root-1",
      .relativePath = "movie.txt",
      .size = static_cast<int64_t>(std::filesystem::file_size(filePath)),
      .mtime = std::filesystem::last_write_time(filePath),
      .contentHash = "local-new",
      .syncState = EntrySyncState::PendingUpload,
      .lastSeenScanId = "scan-1",
  });

  const auto entry = entryRepo.findEntryByPath("root-1", "movie.txt");
  REQUIRE(entry.has_value());
  remoteEntryRepo.markEntryUploaded(entry->id, "remote-file-1", std::nullopt);
  remoteEntryRepo.upsertRemoteEntry({
      .syncRootId = "root-1",
      .remoteId = "remote-file-1",
      .localPath = "movie.txt",
      .remoteType = "file",
      .remoteFileId = std::optional<std::string>("remote-file-1"),
      .remoteFolderId = std::nullopt,
      .remoteParentFolderId = std::nullopt,
      .encryptedName = std::nullopt,
      .encryptedMetadata = std::nullopt,
      .remoteDeletedAt = std::optional<std::string>("2026-06-19T00:00:00Z"),
      .remoteUpdatedAt = "2026-06-19T00:00:00Z",
  });

  const auto root = syncRepo.findSyncRootById("root-1");
  REQUIRE(root.has_value());

  reconciler.reconcileRoot(*root);

  REQUIRE_FALSE(downloadService.lastFileId.has_value());
  const auto conflicted = entryRepo.findEntryByPath("root-1", "movie.txt");
  REQUIRE(conflicted.has_value());
  REQUIRE(conflicted->conflictState ==
          std::optional<std::string>("local_remote_modified"));
}
