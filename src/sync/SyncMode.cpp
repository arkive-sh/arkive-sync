#include "sync/SyncMode.hpp"

std::optional<SyncMode> parseSyncModeDb(std::string_view value) {
  if (value == "upload_only") {
    return SyncMode::UploadOnly;
  }
  if (value == "remote_mirror") {
    return SyncMode::RemoteMirror;
  }
  if (value == "two_way") {
    return SyncMode::TwoWay;
  }

  return std::nullopt;
}

const char *toSyncModeDb(SyncMode mode) {
  switch (mode) {
  case SyncMode::UploadOnly:
    return "upload_only";
  case SyncMode::RemoteMirror:
    return "remote_mirror";
  case SyncMode::TwoWay:
    return "two_way";
  }

  return "two_way";
}
