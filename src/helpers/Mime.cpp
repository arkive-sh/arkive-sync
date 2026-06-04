#include "helpers/Mime.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

namespace {

struct MimeMapping {
  std::string_view extension;
  std::string_view mime;
};

constexpr std::array<MimeMapping, 29> kMimeMappings{{
    {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".png", "image/png"},
    {".gif", "image/gif"},
    {".webp", "image/webp"},
    {".svg", "image/svg+xml"},
    {".bmp", "image/bmp"},
    {".txt", "text/plain"},
    {".md", "text/markdown"},
    {".csv", "text/csv"},
    {".json", "application/json"},
    {".pdf", "application/pdf"},
    {".mp4", "video/mp4"},
    {".m4v", "video/mp4"},
    {".mov", "video/quicktime"},
    {".webm", "video/webm"},
    {".mkv", "video/x-matroska"},
    {".mp3", "audio/mpeg"},
    {".m4a", "audio/mp4"},
    {".wav", "audio/wav"},
    {".ogg", "audio/ogg"},
    {".flac", "audio/flac"},
    {".zip", "application/zip"},
    {".tar", "application/x-tar"},
    {".gz", "application/gzip"},
    {".7z", "application/x-7z-compressed"},
    {".rar", "application/vnd.rar"},
    {".epub", "application/epub+zip"},
    {".heic", "image/heic"},
}};

constexpr std::array<std::string_view, 22> kDangerousExtensions{{
    ".exe",  ".msi",  ".bat",  ".cmd", ".com", ".scr", ".ps1", ".vbs",
    ".js",   ".jse",  ".jar",  ".app", ".apk", ".dmg", ".pkg", ".deb",
    ".rpm",  ".sh",   ".csh",  ".ksh", ".so",  ".dll",
}};

std::string normalizedExtension(const std::filesystem::path &path) {
  std::string extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return extension;
}

bool isDangerousExtension(std::string_view extension) {
  return std::find(kDangerousExtensions.begin(), kDangerousExtensions.end(),
                   extension) != kDangerousExtensions.end();
}

} // namespace

std::string inferSafeMimeType(const std::filesystem::path &path) {
  const std::string extension = normalizedExtension(path);
  if (extension.empty() || isDangerousExtension(extension)) {
    return "application/octet-stream";
  }

  const auto it =
      std::find_if(kMimeMappings.begin(), kMimeMappings.end(),
                   [&](const MimeMapping &mapping) {
                     return mapping.extension == extension;
                   });
  if (it == kMimeMappings.end()) {
    return "application/octet-stream";
  }

  return std::string(it->mime);
}

std::string fileExtensionString(const std::filesystem::path &path) {
  return path.has_extension() ? path.extension().string() : "";
}

FileMimeDetails describeFileMime(const std::filesystem::path &path) {
  return FileMimeDetails{
      .name = path.filename().string(),
      .extension = fileExtensionString(path),
      .mime = inferSafeMimeType(path),
  };
}
