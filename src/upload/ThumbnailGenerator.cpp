#include "upload/ThumbnailGenerator.hpp"

#include "helpers/GenUUID.hpp"
#include "helpers/Mime.hpp"

#include <cstddef>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace {

constexpr std::string_view kThumbnailMime = "image/webp";
constexpr uintmax_t kMaxPlainThumbnailBytes = 150 * 1024;

bool startsWith(std::string_view value, std::string_view prefix) {
  return value.size() >= prefix.size() &&
         value.substr(0, prefix.size()) == prefix;
}

bool shouldThumbnail(const std::filesystem::path &path) {
  const std::string mime = inferSafeMimeType(path);
  return startsWith(mime, "image/") || startsWith(mime, "video/");
}

std::filesystem::path executableDir() {
#ifdef _WIN32
  std::wstring buffer(MAX_PATH, L'\0');
  DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                    static_cast<DWORD>(buffer.size()));
  if (length == 0) {
    return {};
  }
  buffer.resize(length);
  return std::filesystem::path(buffer).parent_path();
#elif defined(__APPLE__)
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::string buffer(size, '\0');
  if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
    return {};
  }
  buffer.resize(std::char_traits<char>::length(buffer.c_str()));
  std::error_code error;
  const auto path = std::filesystem::weakly_canonical(buffer, error);
  return error ? std::filesystem::path(buffer).parent_path()
               : path.parent_path();
#else
  std::error_code error;
  const auto path = std::filesystem::read_symlink("/proc/self/exe", error);
  return error ? std::filesystem::path{} : path.parent_path();
#endif
}

#ifdef _WIN32
std::wstring quoteWindowsArg(const std::wstring &arg) {
  std::wstring quoted = L"\"";
  size_t slashes = 0;
  for (const wchar_t ch : arg) {
    if (ch == L'\\') {
      ++slashes;
      continue;
    }
    if (ch == L'"') {
      quoted.append(slashes * 2 + 1, L'\\');
      quoted.push_back(ch);
      slashes = 0;
      continue;
    }
    quoted.append(slashes, L'\\');
    slashes = 0;
    quoted.push_back(ch);
  }
  quoted.append(slashes * 2, L'\\');
  quoted.push_back(L'"');
  return quoted;
}

bool runProcess(const std::filesystem::path &program,
                const std::vector<std::filesystem::path> &args) {
  std::wstring command = quoteWindowsArg(program.wstring());
  for (const auto &arg : args) {
    command.push_back(L' ');
    command += quoteWindowsArg(arg.wstring());
  }

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESHOWWINDOW;
  startup.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION process{};
  std::vector<wchar_t> buffer(command.begin(), command.end());
  buffer.push_back(L'\0');

  if (!CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, FALSE,
                      CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
    return false;
  }

  WaitForSingleObject(process.hProcess, INFINITE);
  DWORD exitCode = 1;
  GetExitCodeProcess(process.hProcess, &exitCode);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return exitCode == 0;
}
#else
bool runProcess(const std::filesystem::path &program,
                const std::vector<std::filesystem::path> &args) {
  std::vector<std::string> strings;
  strings.reserve(args.size() + 1);
  strings.push_back(program.string());
  for (const auto &arg : args) {
    strings.push_back(arg.string());
  }

  std::vector<char *> argv;
  argv.reserve(strings.size() + 1);
  for (auto &arg : strings) {
    argv.push_back(arg.data());
  }
  argv.push_back(nullptr);

  const pid_t pid = fork();
  if (pid == 0) {
    const int devNull = open("/dev/null", O_WRONLY);
    if (devNull >= 0) {
      dup2(devNull, STDOUT_FILENO);
      dup2(devNull, STDERR_FILENO);
      close(devNull);
    }
    if (program.has_parent_path()) {
      execv(argv[0], argv.data());
    } else {
      execvp(argv[0], argv.data());
    }
    _exit(127);
  }
  if (pid < 0) {
    return false;
  }

  int status = 0;
  if (waitpid(pid, &status, 0) < 0) {
    return false;
  }
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
#endif

std::filesystem::path ffmpegPath() {
  const auto bundled = executableDir() / "bin" /
#ifdef _WIN32
                       "ffmpeg.exe";
#else
                       "ffmpeg";
#endif
  std::error_code error;
  if (!bundled.empty() && std::filesystem::exists(bundled, error)) {
    return bundled;
  }
  return "ffmpeg";
}

std::vector<uint8_t> readBytes(const std::filesystem::path &path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream.is_open()) {
    return {};
  }
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(stream),
                              std::istreambuf_iterator<char>());
}

uint32_t le24(const std::vector<uint8_t> &bytes, size_t offset) {
  return static_cast<uint32_t>(bytes[offset]) |
         (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
         (static_cast<uint32_t>(bytes[offset + 2]) << 16);
}

std::optional<std::pair<int, int>> webpSize(const std::vector<uint8_t> &bytes) {
  if (bytes.size() < 30 ||
      std::string_view(reinterpret_cast<const char *>(bytes.data()), 4) !=
          "RIFF" ||
      std::string_view(reinterpret_cast<const char *>(bytes.data() + 8), 4) !=
          "WEBP") {
    return std::nullopt;
  }

  const std::string_view chunk(
      reinterpret_cast<const char *>(bytes.data() + 12), 4);
  if (chunk == "VP8X") {
    return std::pair<int, int>{static_cast<int>(le24(bytes, 24) + 1),
                               static_cast<int>(le24(bytes, 27) + 1)};
  }
  if (chunk == "VP8 " && bytes.size() >= 30) {
    return std::pair<int, int>{
        static_cast<int>((bytes[26] | (bytes[27] << 8)) & 0x3fff),
        static_cast<int>((bytes[28] | (bytes[29] << 8)) & 0x3fff)};
  }
  if (chunk == "VP8L" && bytes.size() >= 25) {
    const uint32_t bits = static_cast<uint32_t>(bytes[21]) |
                          (static_cast<uint32_t>(bytes[22]) << 8) |
                          (static_cast<uint32_t>(bytes[23]) << 16) |
                          (static_cast<uint32_t>(bytes[24]) << 24);
    return std::pair<int, int>{static_cast<int>((bits & 0x3fff) + 1),
                               static_cast<int>(((bits >> 14) & 0x3fff) + 1)};
  }
  return std::nullopt;
}

} // namespace

std::optional<UploadThumbnail>
generateUploadThumbnail(const std::filesystem::path &path) {
  if (!shouldThumbnail(path)) {
    return std::nullopt;
  }

  const auto output = std::filesystem::temp_directory_path() /
                      ("arkive-thumbnail-" + generateUUID() + ".webp");
  const bool video = startsWith(inferSafeMimeType(path), "video/");

  std::vector<std::filesystem::path> args{
      "-hide_banner",
      "-loglevel",
      "error",
      "-y",
  };
  if (video) {
    args.insert(args.end(), {"-ss", "00:00:30.000"});
  }
  args.insert(args.end(),
              {
                  "-i",
                  path,
      "-frames:v",
      "1",
      "-vf",
      "scale='min(320,iw)':'min(320,ih)':force_original_aspect_ratio=decrease",
      "-c:v",
                  "libwebp",
                  "-quality",
                  "70",
                  "-f",
                  "webp",
                  output,
              });

  if (!runProcess(ffmpegPath(), args)) {
    std::filesystem::remove(output);
    return std::nullopt;
  }

  std::error_code error;
  if (std::filesystem::file_size(output, error) > kMaxPlainThumbnailBytes) {
    std::filesystem::remove(output);
    return std::nullopt;
  }

  std::vector<uint8_t> bytes = readBytes(output);
  std::filesystem::remove(output);
  const auto size = webpSize(bytes);
  if (bytes.empty() || !size.has_value() || size->first <= 0 ||
      size->second <= 0) {
    return std::nullopt;
  }

  return UploadThumbnail{
      .bytes = std::move(bytes),
      .mime = std::string(kThumbnailMime),
      .width = size->first,
      .height = size->second,
  };
}
