#pragma once

#include <filesystem>
#include <string>

std::string inferSafeMimeType(const std::filesystem::path &path);
