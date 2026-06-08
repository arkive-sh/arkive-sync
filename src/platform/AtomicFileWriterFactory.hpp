#pragma once

#include "fs/AtomicFileWriter.hpp"

#include <filesystem>
#include <memory>

std::unique_ptr<AtomicFileWriter>
createAtomicFileWriter(const std::filesystem::path &finalPath);
