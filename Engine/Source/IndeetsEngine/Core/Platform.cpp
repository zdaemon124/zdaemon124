#include "IndeetsEngine/Core/Platform.h"

#include <fstream>

#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace ie::Platform {

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
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec))
        return {};
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        return {};
    std::streamoff size = file.tellg();
    if (size <= 0)
        return {};
    std::vector<char> data(static_cast<size_t>(size));
    file.seekg(0);
    file.read(data.data(), static_cast<std::streamsize>(data.size()));
    return data;
}

std::string PathToUtf8(const std::filesystem::path& path)
{
    std::u8string u8 = path.generic_u8string();
    return std::string(u8.begin(), u8.end());
}

std::filesystem::path Utf8ToPath(const std::string& utf8)
{
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
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

void OpenWithDefaultApp(const std::filesystem::path& file)
{
#ifdef _WIN32
    ShellExecuteW(nullptr, L"open", file.wstring().c_str(), nullptr, file.parent_path().wstring().c_str(), SW_SHOWNORMAL);
#else
    std::string command = "xdg-open \"" + file.string() + "\" >/dev/null 2>&1 &";
    [[maybe_unused]] int result = std::system(command.c_str());
#endif
}

} // namespace ie::Platform
