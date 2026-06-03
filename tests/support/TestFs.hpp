#pragma once

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

class TempDir {
public:
  TempDir() {
    path_ = std::filesystem::temp_directory_path() /
            ("arkive-sync-test-" +
             std::to_string(static_cast<long long>(
                 std::chrono::steady_clock::now().time_since_epoch().count())) +
             "-" + std::to_string(std::rand()));
    std::filesystem::create_directories(path_);
  }

  ~TempDir() { std::filesystem::remove_all(path_); }

  const std::filesystem::path &path() const { return path_; }

private:
  std::filesystem::path path_;
};

inline void writeFile(const std::filesystem::path &path,
                      const std::string &contents) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  REQUIRE(stream.is_open());
  stream << contents;
}
