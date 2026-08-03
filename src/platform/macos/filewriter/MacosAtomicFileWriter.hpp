#pragma once

#include "fs/AtomicFileWriter.hpp"

#include <filesystem>
#include <span>

class MacosAtomicFileWriter final : public AtomicFileWriter {
public:
  explicit MacosAtomicFileWriter(std::filesystem::path finalPath);
  ~MacosAtomicFileWriter() override;

  void preallocate(std::uint64_t size) override;
  void writeAt(std::uint64_t offset,
               std::span<const std::uint8_t> data) override;
  void commit() override;
  void rollback() noexcept override;

private:
  std::filesystem::path finalPath_;
  std::filesystem::path tempPath_;
  int fd_{-1};
  bool committed_{false};

  void closeFd();
};
