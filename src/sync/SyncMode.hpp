#pragma once

#include <optional>
#include <string_view>

enum class SyncMode {
  UploadOnly,
  RemoteMirror,
  TwoWay,
};

std::optional<SyncMode> parseSyncModeDb(std::string_view value);
const char *toSyncModeDb(SyncMode mode);
