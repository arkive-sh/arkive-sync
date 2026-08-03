#pragma once

#include <filesystem>
#include <string>

std::filesystem::path appDataDir();
std::filesystem::path databasePath();
std::filesystem::path cookieJarPath();
std::string ipcEndpoint();
