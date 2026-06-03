#pragma once

#include <filesystem>
#include <string>

class PathCodec {
public:
  static std::string toDbRelative(const std::filesystem::path &relative);
  static std::filesystem::path fromDbRelative(const std::string &dbPath);
  static std::filesystem::path joinRoot(const std::filesystem::path &root,
                                        const std::string &dbRelativePath);
};
