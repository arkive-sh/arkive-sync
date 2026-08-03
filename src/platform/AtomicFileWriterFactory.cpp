#include "platform/AtomicFileWriterFactory.hpp"

#if defined(__linux__)
#include "platform/linux/filewriter/LinuxAtomicFileWriter.hpp"
#endif

#include <stdexcept>

std::unique_ptr<AtomicFileWriter>
createAtomicFileWriter(const std::filesystem::path &finalPath) {
#if defined(__linux__)
  return std::make_unique<LinuxAtomicFileWriter>(finalPath);
#else
  (void)finalPath;
  throw std::runtime_error("AtomicFileWriter is not implemented on this platform");
#endif
}
