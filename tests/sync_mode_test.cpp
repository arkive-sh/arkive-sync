#include "sync/SyncMode.hpp"

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>

TEST_CASE("SyncMode defines unique stable keys") {
  std::set<std::string> keys;

  for (const auto &mode : kSyncModes) {
    REQUIRE_FALSE(mode.key.empty());
    REQUIRE(keys.insert(std::string(mode.key)).second);
  }
}

TEST_CASE("SyncMode parser resolves every declared mode key") {
  for (const auto &mode : kSyncModes) {
    const auto parsed = parseSyncMode(mode.key);
    REQUIRE(parsed.has_value());
    REQUIRE(*parsed == mode.mode);
  }
}

TEST_CASE("SyncMode default is local mirror") {
  const auto &mode = defaultSyncMode();

  REQUIRE(mode.mode == SyncMode::LocalMirror);
  REQUIRE(mode.direction == SyncModeDirection::LocalToRemote);
  REQUIRE(mode.localCreates);
  REQUIRE(mode.localUpdates);
  REQUIRE(mode.localDeletes);
  REQUIRE_FALSE(mode.remoteCreates);
  REQUIRE_FALSE(mode.remoteUpdates);
  REQUIRE_FALSE(mode.remoteDeletes);
  REQUIRE_FALSE(mode.preservesHistory);
}

TEST_CASE("SyncMode marks bidirectional modes as conflict-aware") {
  const auto *mirror = findSyncMode(SyncMode::BidirectionalMirror);
  const auto *versioned = findSyncMode(SyncMode::BidirectionalVersioned);

  REQUIRE(mirror != nullptr);
  REQUIRE(versioned != nullptr);
  REQUIRE(mirror->needsConflictResolution);
  REQUIRE(versioned->needsConflictResolution);
}
