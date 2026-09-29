#include "ZEngine/Core/Platform.h"

#include <fstream>

#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace ze::Platform {

std::filesystem::path ExecutableDir()
{
#ifdef _WIN32
    wchar_t buffer[MAX_PATH];
    DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return std::filesystem::path(std::wstring(buffer, length)).parent_path();
#else
    std::error_code ec;
    auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::filesystem::current_path() : exe.parent_path();
#endif
}

std::vector<char> ReadBinaryFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        return {};
    std::vector<char> data(static_cast<size_t>(file.tellg()));
    file.seekg(0);
    file.read(data.data(), static_cast<std::streamsize>(data.size()));
    return data;
}

void OpenInFileBrowser(const std::filesystem::path& path)
{
    std::filesystem::path folder = std::filesystem::is_directory(path) ? path : path.parent_path();
#ifdef _WIN32
    ShellExecuteW(nullptr, L"open", folder.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    std::string command = "xdg-open \"" + folder.string() + "\" >/dev/null 2>&1 &";
    [[maybe_unused]] int result = std::system(command.c_str());
#endif
}

} // namespace ze::Platform
