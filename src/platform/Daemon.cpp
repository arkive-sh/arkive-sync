#include "platform/Daemon.hpp"

#include "fs/FileWatcher.hpp"

#if defined(__linux__)
#include "platform/linux/daemon/LinuxDaemon.hpp"
#endif

std::unique_ptr<Daemon> Daemon::create() {
#if defined(__linux__)
  return std::make_unique<LinuxDaemon>(IFileWatcher::create());
#elif defined(__APPLE__)
  throw std::runtime_error("Daemon is not implemented on macOS yet");
#elif defined(_WIN32)
  throw std::runtime_error("Daemon is not implemented on Windows yet");
#else
  throw std::runtime_error("Daemon is not implemented on this platform");
#endif
}
