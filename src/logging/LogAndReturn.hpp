#pragma once

#include "fs/FileWatcher.hpp"

#include <spdlog/spdlog.h>

inline std::optional<FileEvent> logAndReturn(FileEvent event) {
  if (event.type == FileEventType::Renamed && event.oldPath.has_value()) {
    spdlog::info("Watcher event {} root={} old={} new={} dir={} cookie={}",
                 eventTypeName(event.type), event.rootId,
                 event.oldPath->string(), event.path.string(),
                 event.isDirectory, event.cookie);
  } else {
    spdlog::info("Watcher event {} root={} path={} dir={} cookie={}",
                 eventTypeName(event.type), event.rootId, event.path.string(),
                 event.isDirectory, event.cookie);
  }

  return event;
}
