#include "platform/SecureStorage.hpp"

#include <stdexcept>

#if defined(__linux__)
#include "platform/linux/SecureStorage.hpp"
#endif

std::unique_ptr<SecureStorage> SecureStorage::create() {
#if defined(__linux__)
  return std::make_unique<LinuxSecureStorage>();
#elif defined(_WIN32)
  throw std::runtime_error("SecureStorage is not implemented on Windows yet");
#elif defined(__APPLE__)
  throw std::runtime_error("SecureStorage is not implemented on macOS yet");
#else
  throw std::runtime_error("SecureStorage is not implemented on this platform");
#endif
}
