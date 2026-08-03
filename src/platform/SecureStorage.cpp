#include "platform/SecureStorage.hpp"

#include <stdexcept>

#if defined(__linux__)
#include "platform/linux/SecureStorage.hpp"
#elif defined(__APPLE__)
#include "platform/macos/SecureStorage.hpp"
#elif defined(_WIN32)
#include "platform/windows/SecureStorage.hpp"
#endif

std::unique_ptr<SecureStorage> SecureStorage::create() {
#if defined(__linux__)
  return std::make_unique<LinuxSecureStorage>();
#elif defined(__APPLE__)
  return std::make_unique<MacosSecureStorage>();
#elif defined(_WIN32)
  return std::make_unique<WindowsSecureStorage>();
#else
  throw std::runtime_error("SecureStorage is not implemented on this platform");
#endif
}
