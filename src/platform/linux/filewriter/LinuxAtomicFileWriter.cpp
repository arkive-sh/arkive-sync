#include "platform/linux/filewriter/LinuxAtomicFileWriter.hpp"
#include "helpers/Hex.hpp"
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace {

std::runtime_error sysError(const char *message) {
  return std::runtime_error(std::string(message) + ": " + std::strerror(errno));
}

} // namespace

LinuxAtomicFileWriter::LinuxAtomicFileWriter(std::filesystem::path finalPath)
    : finalPath_(std::move(finalPath)) {
  std::filesystem::create_directories(finalPath_.parent_path());

  tempPath_ = finalPath_.string() + ".arkive-" + generateRandomHex(8) + ".tmp";

  fd_ =
      ::open(tempPath_.c_str(), O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0600);
  if (fd_ < 0) {
    throw sysError("open temp file failed");
  }
}

LinuxAtomicFileWriter::~LinuxAtomicFileWriter() { rollback(); }

void LinuxAtomicFileWriter::preallocate(std::uint64_t size) {
  const int rc = ::posix_fallocate(fd_, 0, static_cast<off_t>(size));

  if (rc != 0) {
    throw std::runtime_error("posix_fallocate failed: " +
                             std::string(std::strerror(rc)));
  }
}

void LinuxAtomicFileWriter::writeAt(std::uint64_t offset,
                                    std::span<const std::uint8_t> data) {
  while (!data.empty()) {
    const ssize_t written =
        ::pwrite(fd_, data.data(), data.size(), static_cast<off_t>(offset));

    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }

      throw sysError("pwrite failed");
    }

    if (written == 0) {
      throw std::runtime_error("pwrite wrote 0 bytes");
    }

    data = data.subspan(static_cast<std::size_t>(written));
    offset += static_cast<std::uint64_t>(written);
  }
}

void LinuxAtomicFileWriter::commit() {
  if (::fdatasync(fd_) != 0) {
    throw sysError("fdatasync failed");
  }

  closeFd();

  std::filesystem::rename(tempPath_, finalPath_);

  const int dirFd = ::open(finalPath_.parent_path().c_str(),
                           O_RDONLY | O_DIRECTORY | O_CLOEXEC);

  if (dirFd >= 0) {
    ::fsync(dirFd);
    ::close(dirFd);
  }

  committed_ = true;
}

void LinuxAtomicFileWriter::rollback() noexcept {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }

  if (!committed_) {
    std::error_code ec;
    std::filesystem::remove(tempPath_, ec);
  }
}

void LinuxAtomicFileWriter::closeFd() {
  if (fd_ >= 0) {
    if (::close(fd_) != 0) {
      fd_ = -1;
      throw sysError("close failed");
    }

    fd_ = -1;
  }
}
