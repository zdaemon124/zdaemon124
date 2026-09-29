#include "IndeetsEngine/Scripting/DotNetHost.h"

#include "IndeetsEngine/Core/Platform.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

// Declarations from the .NET hosting headers (hostfxr.h, coreclr_delegates.h; MIT licensed),
// reduced to what is used here, so no SDK "nethost" package is needed to build.
namespace {

#ifdef _WIN32
using char_t = wchar_t;
#define IE_HOSTFXR_CALLTYPE __cdecl
#define IE_DELEGATE_CALLTYPE __stdcall
#else
using char_t = char;
#define IE_HOSTFXR_CALLTYPE
#define IE_DELEGATE_CALLTYPE
#endif

enum HostFxrDelegateType {
    hdt_com_activation,
    hdt_load_in_memory_assembly,
    hdt_winrt_activation,
    hdt_com_register,
    hdt_com_unregister,
    hdt_load_assembly_and_get_function_pointer,
    hdt_get_function_pointer,
};

struct HostFxrInitializeParameters {
    size_t size;
    const char_t* host_path;
    const char_t* dotnet_root;
};

using HostFxrHandle = void*;
using InitForConfigFn = int32_t(IE_HOSTFXR_CALLTYPE*)(const char_t* runtimeConfigPath,
                                                       const HostFxrInitializeParameters* parameters,
                                                       HostFxrHandle* hostContextHandle);
using GetDelegateFn = int32_t(IE_HOSTFXR_CALLTYPE*)(const HostFxrHandle hostContextHandle, HostFxrDelegateType type,
                                                     void** delegate);
using CloseFn = int32_t(IE_HOSTFXR_CALLTYPE*)(const HostFxrHandle hostContextHandle);
using ErrorWriterFn = void(IE_HOSTFXR_CALLTYPE*)(const char_t* message);
using SetErrorWriterFn = ErrorWriterFn(IE_HOSTFXR_CALLTYPE*)(ErrorWriterFn writer);
using LoadAssemblyAndGetFunctionPointerFn = int(IE_DELEGATE_CALLTYPE*)(const char_t* assemblyPath, const char_t* typeName,
                                                                        const char_t* methodName,
                                                                        const char_t* delegateTypeName, void* reserved,
                                                                        void** delegate);
const char_t* const kUnmanagedCallersOnly = reinterpret_cast<const char_t*>(-1);

#ifdef _WIN32
constexpr const char* kHostFxrName = "hostfxr.dll";
#elif defined(__APPLE__)
constexpr const char* kHostFxrName = "libhostfxr.dylib";
#else
constexpr const char* kHostFxrName = "libhostfxr.so";
#endif

std::string g_HostErrors;

void IE_HOSTFXR_CALLTYPE CollectHostError(const char_t* message)
{
#ifdef _WIN32
    g_HostErrors += ie::Platform::PathToUtf8(std::filesystem::path(message));
#else
    g_HostErrors += message;
#endif
    g_HostErrors += '\n';
}

std::basic_string<char_t> ToNative(const std::string& utf8)
{
#ifdef _WIN32
    return ie::Platform::Utf8ToPath(utf8).wstring();
#else
    return utf8;
#endif
}

void* OpenLibrary(const std::filesystem::path& path)
{
#ifdef _WIN32
    return LoadLibraryW(path.wstring().c_str());
#else
    return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

void* Symbol(void* library, const char* name)
{
#ifdef _WIN32
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(library), name));
#else
    return dlsym(library, name);
#endif
}

// "8.0.11" < "10.0.0": compares the dotted numbers, not the text.
std::vector<int> VersionParts(const std::string& version)
{
    std::vector<int> parts;
    std::stringstream stream(version);
    std::string item;
    while (std::getline(stream, item, '.'))
        parts.push_back(std::atoi(item.c_str()));
    return parts;
}

std::filesystem::path NewestHostFxr(const std::filesystem::path& dotnetRoot)
{
    std::error_code ec;
    std::filesystem::path fxrDir = dotnetRoot / "host" / "fxr";
    if (!std::filesystem::is_directory(fxrDir, ec))
        return {};
    std::filesystem::path best;
    std::vector<int> bestVersion;
    for (const auto& entry : std::filesystem::directory_iterator(fxrDir, ec)) {
        std::filesystem::path candidate = entry.path() / kHostFxrName;
        if (!std::filesystem::is_regular_file(candidate, ec))
            continue;
        std::vector<int> version = VersionParts(entry.path().filename().string());
        if (best.empty() || version > bestVersion) {
            best = candidate;
            bestVersion = version;
        }
    }
    return best;
}

std::string Env(const char* name)
{
    const char* value = std::getenv(name);
    return value ? value : "";
}

std::vector<std::filesystem::path> DotNetRootCandidates()
{
    std::vector<std::filesystem::path> roots;
    for (const char* name : {"DOTNET_ROOT", "DOTNET_ROOT_X64"})
        if (std::string v = Env(name); !v.empty())
            roots.emplace_back(ie::Platform::Utf8ToPath(v));
#ifdef _WIN32
    for (const char* name : {"ProgramFiles", "ProgramW6432"})
        if (std::string v = Env(name); !v.empty())
            roots.emplace_back(ie::Platform::Utf8ToPath(v) / "dotnet");
    if (std::string v = Env("LOCALAPPDATA"); !v.empty())
        roots.emplace_back(ie::Platform::Utf8ToPath(v) / "Microsoft" / "dotnet");
#else
    for (const char* dir : {"/usr/share/dotnet", "/usr/lib/dotnet", "/usr/lib64/dotnet", "/usr/local/share/dotnet",
                            "/opt/dotnet", "/usr/local/lib/dotnet", "/snap/dotnet-sdk/current"})
        roots.emplace_back(dir);
    if (std::string home = Env("HOME"); !home.empty())
        roots.emplace_back(std::filesystem::path(home) / ".dotnet");
#endif
    // `dotnet` on PATH (possibly a symlink into the real install folder).
#ifdef _WIN32
    const char separator = ';';
    const char* exe = "dotnet.exe";
#else
    const char separator = ':';
    const char* exe = "dotnet";
#endif
    std::stringstream path(Env("PATH"));
    std::string dir;
    std::error_code ec;
    while (std::getline(path, dir, separator)) {
        if (dir.empty())
            continue;
        std::filesystem::path candidate = ie::Platform::Utf8ToPath(dir) / exe;
        if (std::filesystem::exists(candidate, ec))
            roots.push_back(std::filesystem::weakly_canonical(candidate, ec).parent_path());
    }
    return roots;
}

} // namespace

namespace ie {

DotNetHost::~DotNetHost()
{
    // The runtime cannot be unloaded from a process; only the host context is released.
    if (m_Context && m_Close)
        reinterpret_cast<CloseFn>(m_Close)(m_Context);
}

bool DotNetHost::LoadHostFxr()
{
    for (const std::filesystem::path& root : DotNetRootCandidates()) {
        std::filesystem::path fxr = NewestHostFxr(root);
        if (fxr.empty())
            continue;
        m_Library = OpenLibrary(fxr);
        if (!m_Library)
            continue;
        m_InitForConfig = Symbol(m_Library, "hostfxr_initialize_for_runtime_config");
        m_GetDelegate = Symbol(m_Library, "hostfxr_get_runtime_delegate");
        m_Close = Symbol(m_Library, "hostfxr_close");
        if (auto setWriter = reinterpret_cast<SetErrorWriterFn>(Symbol(m_Library, "hostfxr_set_error_writer")))
            setWriter(CollectHostError);
        if (m_InitForConfig && m_GetDelegate && m_Close) {
            m_DotNetRoot = root;
            return true;
        }
    }
    m_Error = "The .NET runtime was not found. Install the .NET 8 (or newer) Desktop Runtime: "
              "https://dotnet.microsoft.com/download/dotnet/8.0";
    return false;
}

bool DotNetHost::Initialize(const std::filesystem::path& runtimeConfig)
{
    if (IsInitialized())
        return true;
    if (!m_Library && !LoadHostFxr())
        return false;

    std::error_code ec;
    if (!std::filesystem::is_regular_file(runtimeConfig, ec)) {
        m_Error = "Missing " + Platform::PathToUtf8(runtimeConfig) + " (the managed engine files were not deployed)";
        return false;
    }

    auto rootNative = m_DotNetRoot.native();
    HostFxrInitializeParameters parameters{sizeof(HostFxrInitializeParameters), nullptr, rootNative.c_str()};
    g_HostErrors.clear();
    int32_t rc = reinterpret_cast<InitForConfigFn>(m_InitForConfig)(runtimeConfig.c_str(), &parameters, &m_Context);
    if (rc < 0 || !m_Context) {
        std::stringstream message;
        message << "Could not start the .NET runtime (error 0x" << std::hex << static_cast<uint32_t>(rc) << ")";
        if (!g_HostErrors.empty())
            message << ": " << g_HostErrors;
        m_Error = message.str();
        m_Context = nullptr;
        return false;
    }

    void* loadAssembly = nullptr;
    rc = reinterpret_cast<GetDelegateFn>(m_GetDelegate)(m_Context, hdt_load_assembly_and_get_function_pointer,
                                                         &loadAssembly);
    if (rc < 0 || !loadAssembly) {
        m_Error = "Could not get the .NET assembly loader delegate";
        return false;
    }
    m_LoadAssembly = loadAssembly;
    return true;
}

void* DotNetHost::GetFunction(const std::filesystem::path& assembly, const std::string& typeName,
                              const std::string& method)
{
    if (!m_LoadAssembly)
        return nullptr;
    auto typeNative = ToNative(typeName);
    auto methodNative = ToNative(method);
    void* function = nullptr;
    g_HostErrors.clear();
    int rc = reinterpret_cast<LoadAssemblyAndGetFunctionPointerFn>(m_LoadAssembly)(
        assembly.c_str(), typeNative.c_str(), methodNative.c_str(), kUnmanagedCallersOnly, nullptr, &function);
    if (rc < 0 || !function) {
        std::stringstream message;
        message << "Managed entry point " << typeName << "." << method << " not found (error 0x" << std::hex
                << static_cast<uint32_t>(rc) << ")";
        if (!g_HostErrors.empty())
            message << ": " << g_HostErrors;
        m_Error = message.str();
        return nullptr;
    }
    return function;
}

} // namespace ie
