#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace ze::Platform {

// Directory that contains the running executable.
std::filesystem::path ExecutableDir();

// Reads a whole file as bytes. Returns an empty vector on failure.
std::vector<char> ReadBinaryFile(const std::filesystem::path& path);

// UTF-8 <-> path conversion that also works for non-ASCII names on Windows.
std::string PathToUtf8(const std::filesystem::path& path);
std::filesystem::path Utf8ToPath(const std::string& utf8);

// Opens a folder (or the folder containing a file) in the system file manager.
void OpenInFileBrowser(const std::filesystem::path& path);

} // namespace ze::Platform
