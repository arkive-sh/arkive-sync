#include "platform/windows/filewriter/WindowsAtomicFileWriter.hpp"

#include "helpers/Hex.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>

namespace {

std::system_error winError(const char *message) {
  return std::system_error(static_cast<int>(GetLastError()),
                           std::system_category(), message);
}

LARGE_INTEGER largeInteger(std::uint64_t value) {
  LARGE_INTEGER result{};
  result.QuadPart = static_cast<LONGLONG>(value);
  return result;
}

} // namespace

WindowsAtomicFileWriter::WindowsAtomicFileWriter(
    std::filesystem::path finalPath)
    : finalPath_(std::move(finalPath)) {
  std::filesystem::create_directories(finalPath_.parent_path());

  tempPath_ = finalPath_.wstring() + L".arkive-" +
              std::filesystem::path(generateRandomHex(8)).wstring() + L".tmp";

  file_ = CreateFileW(tempPath_.c_str(), GENERIC_WRITE, 0, nullptr,
                      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file_ == INVALID_HANDLE_VALUE) {
    throw winError("CreateFileW temp file failed");
  }
}

WindowsAtomicFileWriter::~WindowsAtomicFileWriter() { rollback(); }

void WindowsAtomicFileWriter::preallocate(std::uint64_t size) {
  auto distance = largeInteger(size);
  if (!SetFilePointerEx(file_, distance, nullptr, FILE_BEGIN)) {
    throw winError("SetFilePointerEx failed");
  }
  if (!SetEndOfFile(file_)) {
    throw winError("SetEndOfFile failed");
  }
}

void WindowsAtomicFileWriter::writeAt(std::uint64_t offset,
                                      std::span<const std::uint8_t> data) {
  while (!data.empty()) {
    auto distance = largeInteger(offset);
    if (!SetFilePointerEx(file_, distance, nullptr, FILE_BEGIN)) {
      throw winError("SetFilePointerEx failed");
    }

    const auto size =
        static_cast<DWORD>(std::min<std::size_t>(data.size(), MAXDWORD));
    DWORD written = 0;
    if (!WriteFile(file_, data.data(), size, &written, nullptr)) {
      throw winError("WriteFile failed");
    }
    if (written == 0) {
      throw std::runtime_error("WriteFile wrote 0 bytes");
    }

    data = data.subspan(written);
    offset += written;
  }
}

void WindowsAtomicFileWriter::commit() {
  if (!FlushFileBuffers(file_)) {
    throw winError("FlushFileBuffers failed");
  }

  closeFile();

  if (!MoveFileExW(tempPath_.c_str(), finalPath_.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    throw winError("MoveFileExW failed");
  }

  committed_ = true;
}

void WindowsAtomicFileWriter::rollback() noexcept {
  if (file_ != INVALID_HANDLE_VALUE) {
    CloseHandle(file_);
    file_ = INVALID_HANDLE_VALUE;
  }

  if (!committed_) {
    DeleteFileW(tempPath_.c_str());
  }
}

void WindowsAtomicFileWriter::closeFile() {
  if (file_ != INVALID_HANDLE_VALUE) {
    if (!CloseHandle(file_)) {
      file_ = INVALID_HANDLE_VALUE;
      throw winError("CloseHandle failed");
    }
    file_ = INVALID_HANDLE_VALUE;
  }
}
