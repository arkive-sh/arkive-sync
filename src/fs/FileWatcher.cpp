#include "fs/FileWatcher.hpp"

#if defined(__linux__)
#include "platform/linux/watcher/INotifyWatcher.hpp"
#elif defined(__APPLE__)
#include "platform/macos/watcher/FSEventsWatcher.hpp"
#elif defined(_WIN32)
#include "platform/windows/watcher/ReadDirectoryChangesWatcher.hpp"
#endif

#include <stdexcept>

std::unique_ptr<IFileWatcher> IFileWatcher::create() {
#if defined(__linux__)
  return std::make_unique<InotifyWatcher>();
#elif defined(__APPLE__)
  return std::make_unique<FSEventsWatcher>();
#elif defined(_WIN32)
  return std::make_unique<ReadDirectoryChangesWatcher>();
#else
  throw std::runtime_error("FileWatcher is not implemented on this platform");
#endif
}
