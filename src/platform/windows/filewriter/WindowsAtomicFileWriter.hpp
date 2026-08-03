#pragma once

#include "fs/AtomicFileWriter.hpp"

#include <filesystem>
#include <span>

#include <windows.h>

class WindowsAtomicFileWriter final : public AtomicFileWriter {
public:
  explicit WindowsAtomicFileWriter(std::filesystem::path finalPath);
  ~WindowsAtomicFileWriter() override;

  void preallocate(std::uint64_t size) override;
  void writeAt(std::uint64_t offset,
               std::span<const std::uint8_t> data) override;
  void commit() override;
  void rollback() noexcept override;

private:
  std::filesystem::path finalPath_;
  std::filesystem::path tempPath_;
  HANDLE file_{INVALID_HANDLE_VALUE};
  bool committed_{false};

  void closeFile();
};
