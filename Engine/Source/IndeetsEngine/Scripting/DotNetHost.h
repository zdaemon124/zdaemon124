#pragma once

#include <filesystem>
#include <string>

namespace ie {

// Hosts the .NET runtime through hostfxr (the same way `dotnet app.dll` does).
// hostfxr is found at run time in the installed .NET (DOTNET_ROOT, the standard install
// folders or `dotnet` on PATH), so building the engine needs no .NET SDK and players
// only need the .NET 8+ runtime.
class DotNetHost {
public:
    DotNetHost() = default;
    ~DotNetHost();

    DotNetHost(const DotNetHost&) = delete;
    DotNetHost& operator=(const DotNetHost&) = delete;

    // Starts the runtime described by `runtimeConfig` (<assembly>.runtimeconfig.json).
    bool Initialize(const std::filesystem::path& runtimeConfig);
    bool IsInitialized() const { return m_LoadAssembly != nullptr; }

    // Pointer to a static [UnmanagedCallersOnly] method; `typeName` is "Namespace.Type, Assembly".
    void* GetFunction(const std::filesystem::path& assembly, const std::string& typeName, const std::string& method);

    // Why Initialize failed, for the console.
    const std::string& Error() const { return m_Error; }
    const std::filesystem::path& DotNetRoot() const { return m_DotNetRoot; }

private:
    bool LoadHostFxr();

    void* m_Library = nullptr;
    void* m_InitForConfig = nullptr;
    void* m_GetDelegate = nullptr;
    void* m_Close = nullptr;
    void* m_Context = nullptr;
    void* m_LoadAssembly = nullptr;
    std::filesystem::path m_DotNetRoot;
    std::string m_Error;
};

} // namespace ie
