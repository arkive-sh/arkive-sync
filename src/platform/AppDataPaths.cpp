#include "./AppDataPaths.hpp"

#include <cstdlib>
#include <stdexcept>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace {

#if defined(_WIN32)
std::filesystem::path requireWindowsAppDataDirectory() {
  for (const wchar_t *name : {L"LOCALAPPDATA", L"APPDATA"}) {
    const DWORD length = GetEnvironmentVariableW(name, nullptr, 0);
    if (length == 0) {
      continue;
    }

    std::wstring value(length, L'\0');
    const DWORD written =
        GetEnvironmentVariableW(name, value.data(), length);
    if (written == 0) {
      continue;
    }

    value.resize(written);
    return std::filesystem::path(value);
  }

  throw std::runtime_error("LOCALAPPDATA is not set");
}
#else
std::filesystem::path requireHomeDirectory() {
  const char *home = std::getenv("HOME");
  if (home == nullptr || *home == '\0') {
    throw std::runtime_error("HOME is not set");
  }

  return std::filesystem::path(home);
}
#endif

} // namespace

std::filesystem::path appDataDir() {
#if defined(_WIN32)
  return requireWindowsAppDataDirectory() / "arkive-sync";
#elif defined(__APPLE__)
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

std::string ipcEndpoint() {
#if defined(_WIN32)
  return R"(\\.\pipe\arkive-sync)";
#else
  const char *runtimeDir = std::getenv("XDG_RUNTIME_DIR");
  if (runtimeDir != nullptr && *runtimeDir != '\0') {
    return (std::filesystem::path(runtimeDir) / "arkive-sync.sock").string();
  }
  return (appDataDir() / "arkive-sync.sock").string();
#endif
}
