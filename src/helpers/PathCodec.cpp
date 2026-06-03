#include "helpers/PathCodec.hpp"

#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

std::vector<std::string> splitPortablePath(std::string_view path) {
  std::vector<std::string> segments;
  std::size_t start = 0;

  while (start <= path.size()) {
    const std::size_t end = path.find('/', start);
    const std::string_view segment = end == std::string_view::npos
                                         ? path.substr(start)
                                         : path.substr(start, end - start);
    if (!segment.empty()) {
      segments.emplace_back(segment);
    }

    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1;
  }

  return segments;
}

void validateRelativePathComponent(const std::filesystem::path &component) {
  if (component == "." || component.empty()) {
    return;
  }
  if (component == "..") {
    throw std::invalid_argument(
        "relative path cannot contain parent traversal");
  }
}

} // namespace

std::string PathCodec::toDbRelative(const std::filesystem::path &relative) {
  if (relative.is_absolute()) {
    throw std::invalid_argument("db relative path cannot be absolute");
  }

  const std::filesystem::path normalized = relative.lexically_normal();
  if (normalized.empty() || normalized == ".") {
    return "";
  }

  std::string encoded;
  for (const auto &component : normalized) {
    validateRelativePathComponent(component);
    if (component == ".") {
      continue;
    }

    const std::string part = component.generic_string();
    if (part.empty()) {
      continue;
    }

    if (!encoded.empty()) {
      encoded.push_back('/');
    }
    encoded += part;
  }

  return encoded;
}

std::filesystem::path PathCodec::fromDbRelative(const std::string &dbPath) {
  if (dbPath.empty()) {
    return {};
  }

  std::string portable = dbPath;
  for (char &ch : portable) {
    if (ch == '\\') {
      ch = '/';
    }
  }

  if (!portable.empty() && portable.front() == '/') {
    throw std::invalid_argument("db relative path cannot be absolute");
  }

  std::filesystem::path decoded;
  for (const auto &segment : splitPortablePath(portable)) {
    if (segment == ".") {
      continue;
    }
    if (segment == "..") {
      throw std::invalid_argument(
          "db relative path cannot contain parent traversal");
    }
    decoded /= segment;
  }

  return decoded;
}

std::filesystem::path PathCodec::joinRoot(const std::filesystem::path &root,
                                          const std::string &dbRelativePath) {
  return (root / fromDbRelative(dbRelativePath)).lexically_normal();
}
