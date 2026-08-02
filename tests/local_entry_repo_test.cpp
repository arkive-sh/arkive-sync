#include "db/SqliteHelpers.hpp"
#include "repo/EntryRepo.hpp"
#include "repo/LocalEntryRepo.hpp"
#include "repo/RemoteEntryRepo.hpp"
#include "repo/SyncRepo.hpp"

#include "support/TestDatabase.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

void markSyncedThenDeleted(sqlite3 *db, const std::string &entryId) {
  sqlite3_stmt *rawStmt = nullptr;
  REQUIRE(sqlite3_prepare_v2(db,
                             "UPDATE entries SET "
                             "synced_content_hash = local_content_hash, "
                             "remote_deleted_at = CURRENT_TIMESTAMP, "
                             "local_deleted_at = CURRENT_TIMESTAMP, "
                             "sync_state = 'deleted' "
                             "WHERE id = ?;",
                             -1, &rawStmt, nullptr) == SQLITE_OK);
  StmtUniquePtr stmt(rawStmt);
  bindText(db, stmt.get(), 1, entryId);
  REQUIRE(sqlite3_step(stmt.get()) == SQLITE_DONE);
}

} // namespace

TEST_CASE("LocalEntryRepo revives locally recreated deleted files") {
  TestDatabase db;
  SyncRepo syncRepo(db.get());
  EntryRepo entryRepo(db.get());
  LocalEntryRepo localEntryRepo(db.get());
  RemoteEntryRepo remoteEntryRepo(db.get());

  syncRepo.upsertSyncRoot({
      .Id = "root-1",
      .localPath = "/tmp/root-1",
      .folderId = "root-folder-1",
      .enabled = true,
  });

  localEntryRepo.upsertFileEntry({
      .syncRootId = "root-1",
      .relativePath = "file1.txt",
      .size = 5,
      .mtime = std::filesystem::file_time_type{},
      .contentHash = "hash-1",
      .syncState = EntrySyncState::PendingUpload,
      .lastSeenScanId = "scan-1",
  });

  auto entry = entryRepo.findEntryByPath("root-1", "file1.txt");
  REQUIRE(entry.has_value());
  remoteEntryRepo.markEntryUploaded(entry->id, "old-remote-file", "root-folder-1");
  markSyncedThenDeleted(db.get(), entry->id);

  localEntryRepo.upsertFileEntry({
      .syncRootId = "root-1",
      .relativePath = "file1.txt",
      .size = 5,
      .mtime = std::filesystem::file_time_type{},
      .contentHash = "hash-1",
      .syncState = EntrySyncState::Unchanged,
      .lastSeenScanId = "scan-2",
  });

  entry = entryRepo.findEntryByPath("root-1", "file1.txt");
  REQUIRE(entry.has_value());
  REQUIRE(entry->syncState == EntrySyncState::PendingUpload);
  REQUIRE_FALSE(entry->localDeletedAt.has_value());
  REQUIRE_FALSE(entry->remoteDeletedAt.has_value());
  REQUIRE_FALSE(entry->remoteId.has_value());
  REQUIRE_FALSE(entry->remoteFileId.has_value());
  REQUIRE_FALSE(entry->syncedContentHash.has_value());
}

TEST_CASE("LocalEntryRepo revives locally recreated deleted folders") {
  TestDatabase db;
  SyncRepo syncRepo(db.get());
  EntryRepo entryRepo(db.get());
  LocalEntryRepo localEntryRepo(db.get());
  RemoteEntryRepo remoteEntryRepo(db.get());

  syncRepo.upsertSyncRoot({
      .Id = "root-1",
      .localPath = "/tmp/root-1",
      .folderId = "root-folder-1",
      .enabled = true,
  });

  localEntryRepo.upsertDirectoryEntry({
      .syncRootId = "root-1",
      .relativePath = "docs",
      .lastSeenScanId = "scan-1",
  });

  auto entry = entryRepo.findEntryByPath("root-1", "docs");
  REQUIRE(entry.has_value());
  remoteEntryRepo.markFolderCreated(entry->id, "old-remote-folder",
                                    "root-folder-1");
  markSyncedThenDeleted(db.get(), entry->id);

  localEntryRepo.upsertDirectoryEntry({
      .syncRootId = "root-1",
      .relativePath = "docs",
      .lastSeenScanId = "scan-2",
  });

  entry = entryRepo.findEntryByPath("root-1", "docs");
  REQUIRE(entry.has_value());
  REQUIRE(entry->syncState == EntrySyncState::PendingUpload);
  REQUIRE_FALSE(entry->localDeletedAt.has_value());
  REQUIRE_FALSE(entry->remoteDeletedAt.has_value());
  REQUIRE_FALSE(entry->remoteId.has_value());
  REQUIRE_FALSE(entry->parentFolderId.has_value());
}
