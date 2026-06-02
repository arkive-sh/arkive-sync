#include "./AppDataPaths.hpp"
#include <cstdlib>
#include <stdexcept>

namespace {

std::filesystem::path requireHomeDirectory() {
  const char *home = std::getenv("HOME");
  if (home == nullptr || *home == '\0') {
    throw std::runtime_error("HOME is not set");
  }

  return std::filesystem::path(home);
}

} // namespace

std::filesystem::path appDataDir() {
#ifdef __APPLE__
  return requireHomeDirectory() / "Library/Application Support/arkive-sync";
#else
  const char *xdgDataHome = std::getenv("XDG_DATA_HOME");
  if (xdgDataHome != nullptr && *xdgDataHome != '\0') {
    return std::filesystem::path(xdgDataHome) / "arkive-sync";
  }

  return requireHomeDirectory() / ".local/share/arkive-sync";
#endif
}

std::filesystem::path databasePath() { return appDataDir() / "arkive.db"; }

std::filesystem::path cookieJarPath() { return appDataDir() / "cookies.txt"; }
