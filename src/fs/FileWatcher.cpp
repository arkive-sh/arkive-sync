#include "fs/FileWatcher.hpp"

#if defined(__linux__)
#include "platform/linux/watcher/INotifyWatcher.hpp"
#endif

#include <stdexcept>

std::unique_ptr<IFileWatcher> IFileWatcher::create() {
#if defined(__linux__)
  return std::make_unique<InotifyWatcher>();
#elif defined(__APPLE__)
  throw std::runtime_error("FileWatcher is not implemented on macOS yet");
#elif defined(_WIN32)
  throw std::runtime_error("FileWatcher is not implemented on Windows yet");
#else
  throw std::runtime_error("FileWatcher is not implemented on this platform");
#endif
}
