#include "platform/AtomicFileWriterFactory.hpp"

#if defined(__linux__)
#include "platform/linux/filewriter/LinuxAtomicFileWriter.hpp"
#endif

std::unique_ptr<AtomicFileWriter>
createAtomicFileWriter(const std::filesystem::path &finalPath) {
#if defined(__linux__)
  return std::make_unique<LinuxAtomicFileWriter>(finalPath);
#else
  static_assert(false, "AtomicFileWriter not implemented for this platform");
#endif
}
