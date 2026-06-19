#include "db/SqliteHelpers.hpp"
#include "repo/EntryRepo.hpp"
#include "repo/SyncRepo.hpp"
#include "service/SyncReconciler.hpp"

#include "support/TestDatabase.hpp"
#include "support/TestFs.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

TEST_CASE("SyncReconciler applies delete local for remote deleted entries") {
  TestDatabase db;
  TempDir tempDir;
  SyncRepo syncRepo(db.get());
  EntryRepo entryRepo(db.get());
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

  entryRepo.upsertFileEntry({
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
  entryRepo.markEntryUploaded(entry->id, "remote-file-1", std::nullopt);
  entryRepo.upsertRemoteEntry({
      .syncRootId = "root-1",
      .remoteId = "remote-file-1",
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
