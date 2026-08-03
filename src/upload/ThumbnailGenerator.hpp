#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct UploadThumbnail {
  std::vector<uint8_t> bytes;
  std::string mime;
  int width = 0;
  int height = 0;
};

std::optional<UploadThumbnail>
generateUploadThumbnail(const std::filesystem::path &path);
