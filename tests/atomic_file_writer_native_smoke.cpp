#include "platform/AtomicFileWriterFactory.hpp"

#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

std::vector<std::uint8_t> readFile(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

} // namespace

int main() {
  const auto dir = std::filesystem::temp_directory_path() /
                   ("arkive-atomic-smoke-" + std::to_string(std::rand()));
  const auto path = dir / "file.bin";

  try {
    std::filesystem::create_directories(dir);

    {
      std::ofstream existing(path, std::ios::binary);
      existing << "old";
    }

    {
      auto writer = createAtomicFileWriter(path);
      writer->preallocate(6);
      writer->writeAt(3, std::vector<std::uint8_t>{'i', 'v', 'e'});
      writer->writeAt(0, std::vector<std::uint8_t>{'a', 'r', 'k'});
      writer->commit();
    }

    if (readFile(path) != std::vector<std::uint8_t>{'a', 'r', 'k',
                                                    'i', 'v', 'e'}) {
      std::cerr << "atomic writer smoke failed: committed content mismatch\n";
      return 1;
    }

    {
      auto writer = createAtomicFileWriter(path);
      writer->writeAt(0, std::vector<std::uint8_t>{'b', 'a', 'd'});
    }

    if (readFile(path) != std::vector<std::uint8_t>{'a', 'r', 'k',
                                                    'i', 'v', 'e'}) {
      std::cerr << "atomic writer smoke failed: rollback changed file\n";
      return 1;
    }

    std::filesystem::remove_all(dir);
  } catch (const std::exception &error) {
    std::cerr << "atomic writer smoke failed: " << error.what() << "\n";
    std::filesystem::remove_all(dir);
    return 1;
  }

  return 0;
}
