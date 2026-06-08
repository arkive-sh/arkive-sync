#pragma once

#include <cstdint>
#include <span>

class AtomicFileWriter {
public:
  virtual ~AtomicFileWriter() = default;

  virtual void preallocate(std::uint64_t size) = 0;
  virtual void writeAt(std::uint64_t offset, std::span<const std::uint8_t>) = 0;
  virtual void commit() = 0;
  virtual void rollback() noexcept = 0;

  AtomicFileWriter(const AtomicFileWriter &) = delete;
  AtomicFileWriter &operator=(const AtomicFileWriter &) = delete;

protected:
  AtomicFileWriter() = default;
};
