#pragma once

#include <filesystem>
#include <vector>

namespace ze::Platform {

// Directory that contains the running executable.
std::filesystem::path ExecutableDir();

// Reads a whole file as bytes. Returns an empty vector on failure.
std::vector<char> ReadBinaryFile(const std::filesystem::path& path);

// Opens a folder (or the folder containing a file) in the system file manager.
void OpenInFileBrowser(const std::filesystem::path& path);

} // namespace ze::Platform
