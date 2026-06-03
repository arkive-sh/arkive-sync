#include "platform/SecureStorage.hpp"

#include <stdexcept>

std::unique_ptr<SecureStorage> SecureStorage::create() {
  throw std::runtime_error("SecureStorage::create is not available in tests");
}
