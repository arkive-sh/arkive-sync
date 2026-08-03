#include "platform/AtomicFileWriterFactory.hpp"

#if defined(__linux__)
#include "platform/linux/filewriter/LinuxAtomicFileWriter.hpp"
#elif defined(__APPLE__)
#include "platform/macos/filewriter/MacosAtomicFileWriter.hpp"
#elif defined(_WIN32)
#include "platform/windows/filewriter/WindowsAtomicFileWriter.hpp"
#endif

#include <stdexcept>

std::unique_ptr<AtomicFileWriter>
createAtomicFileWriter(const std::filesystem::path &finalPath) {
#if defined(__linux__)
  return std::make_unique<LinuxAtomicFileWriter>(finalPath);
#elif defined(__APPLE__)
  return std::make_unique<MacosAtomicFileWriter>(finalPath);
#elif defined(_WIN32)
  return std::make_unique<WindowsAtomicFileWriter>(finalPath);
#else
  (void)finalPath;
  throw std::runtime_error("AtomicFileWriter is not implemented on this platform");
#endif
}
