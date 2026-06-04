#pragma once

#include <filesystem>
#include <string>

struct FileMimeDetails {
  std::string name;
  std::string extension;
  std::string mime;
};

std::string inferSafeMimeType(const std::filesystem::path &path);
std::string fileExtensionString(const std::filesystem::path &path);
FileMimeDetails describeFileMime(const std::filesystem::path &path);
