#include "./FileHasher.hpp"
#include "../crypto/RustCrypto.hpp"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <stdexcept>
#include <vector>

FileHasher::FileHasher(const std::filesystem::path &path)
    : path_(std::move(path)) {}

std::string FileHasher::hashFile() {
  // open file
  std::ifstream file(path_, std::ios::binary);
  if (!file.is_open()) {
    throw std::runtime_error("File cannot be opened");
  }

  auto hasher = crypto_.createBlake3Hasher();
  std::vector<char> rawBuffer(4 * 1024 * 1024);

  while (file) {
    file.read(rawBuffer.data(), static_cast<std::streamsize>(rawBuffer.size()));
    const std::streamsize bytesRead = file.gcount();
    if (bytesRead <= 0)
      break;

    hasher.update(reinterpret_cast<const uint8_t *>(rawBuffer.data()),
                  static_cast<size_t>(bytesRead));
  }

  if (!file.eof() && file.fail()) {
    throw std::runtime_error("Error occured while reading the file");
  }

  return hasher.finalizeHex();
}
