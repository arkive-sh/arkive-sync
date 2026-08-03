#include "platform/Ipc.hpp"

#include <stdexcept>

#if defined(__linux__)
#include "platform/linux/Ipc.hpp"
#elif defined(__APPLE__)
#include "platform/macos/Ipc.hpp"
#elif defined(_WIN32)
#include "platform/windows/Ipc.hpp"
#endif

std::unique_ptr<IpcServer> IpcServer::create(const std::string &endpoint) {
#if defined(__linux__)
  return std::make_unique<LinuxIpcServer>(endpoint);
#elif defined(__APPLE__)
  return std::make_unique<MacosIpcServer>(endpoint);
#elif defined(_WIN32)
  return std::make_unique<WindowsIpcServer>(endpoint);
#else
  (void)endpoint;
  throw std::runtime_error("IPC is not implemented on this platform");
#endif
}

std::unique_ptr<IpcClient> IpcClient::create(const std::string &endpoint) {
#if defined(__linux__)
  return std::make_unique<LinuxIpcClient>(endpoint);
#elif defined(__APPLE__)
  return std::make_unique<MacosIpcClient>(endpoint);
#elif defined(_WIN32)
  return std::make_unique<WindowsIpcClient>(endpoint);
#else
  (void)endpoint;
  throw std::runtime_error("IPC is not implemented on this platform");
#endif
}
