#include "sync/SyncPolicy.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("SyncPolicy uploads local changes in upload only mode") {
  const SyncEntryState state{
      .localExists = true,
      .remoteExists = true,
      .localDeleted = false,
      .remoteDeleted = false,
      .localDirty = true,
      .remoteDirty = false,
      .isDirectory = false,
      .hasConflict = false,
  };

  REQUIRE(SyncPolicy::decide(state, SyncMode::UploadOnly) ==
          SyncDecision::Upload);
}

TEST_CASE("SyncPolicy downloads remote changes in remote mirror mode") {
  const SyncEntryState state{
      .localExists = true,
      .remoteExists = true,
      .localDeleted = false,
      .remoteDeleted = false,
      .localDirty = false,
      .remoteDirty = true,
      .isDirectory = false,
      .hasConflict = false,
  };

  REQUIRE(SyncPolicy::decide(state, SyncMode::RemoteMirror) ==
          SyncDecision::Download);
}

TEST_CASE("SyncPolicy resolves one sided deletions in two way mode") {
  const SyncEntryState localDelete{
      .localExists = false,
      .remoteExists = true,
      .localDeleted = true,
      .remoteDeleted = false,
      .localDirty = false,
      .remoteDirty = false,
      .isDirectory = false,
      .hasConflict = false,
  };
  const SyncEntryState remoteDelete{
      .localExists = true,
      .remoteExists = false,
      .localDeleted = false,
      .remoteDeleted = true,
      .localDirty = false,
      .remoteDirty = false,
      .isDirectory = false,
      .hasConflict = false,
  };

  REQUIRE(SyncPolicy::decide(localDelete, SyncMode::TwoWay) ==
          SyncDecision::DeleteRemote);
  REQUIRE(SyncPolicy::decide(remoteDelete, SyncMode::TwoWay) ==
          SyncDecision::DeleteLocal);
}

TEST_CASE("SyncPolicy marks diverged two way changes as conflict") {
  const SyncEntryState state{
      .localExists = true,
      .remoteExists = true,
      .localDeleted = false,
      .remoteDeleted = false,
      .localDirty = true,
      .remoteDirty = true,
      .isDirectory = false,
      .hasConflict = false,
  };

  REQUIRE(SyncPolicy::decide(state, SyncMode::TwoWay) ==
          SyncDecision::Conflict);
}
