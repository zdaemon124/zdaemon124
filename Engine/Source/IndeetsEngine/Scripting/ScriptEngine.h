#pragma once

#include "IndeetsEngine/Scene/Scene.h"

#include <glm/vec2.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace ie {

class PhysicsWorld;
struct ContactEvent;

// Project assets for scripts (Resources.Load, asset references in serialized fields, Instantiate of prefabs).
// Paths are relative to the Assets folder. See MakeProjectAssets (Scripting/ProjectAssets.h).
struct ScriptAssetProvider {
    // Resources.Load paths ("Folder/Name", no extension) -> asset paths. `all`: everything below a folder.
    std::function<std::vector<std::string>(const std::string& resourcePath, bool all)> findResources;
    // {"kind": "script" | "prefab" | "model" | "texture" | "material" | "audio" | "text" | "other", "name", ...}
    std::function<nlohmann::json(const std::string& assetPath)> describe;
    // Adds a prefab or model to the scene; returns its root (0 on failure).
    std::function<EntityID(Scene& scene, const std::string& assetPath, EntityID parent)> instantiate;
};

struct ScriptDiagnostic {
    bool error = true;
    std::string file; // absolute path
    int line = 0;
    int column = 0;
    std::string id;   // CS0103, ...
    std::string message;
};

// C# scripting: hosts .NET, compiles the project's scripts with Roslyn (in process) and runs
// MonoBehaviours with Unity's lifecycle against the current Scene. The scripting API is the
// UnityEngine one (Engine/ScriptCore), so Unity scripts compile unchanged where it is covered.
//
// Frame order while playing, as in Unity:
//   FixedUpdate (+ physics step) x N  ->  Update  ->  coroutines  ->  LateUpdate  ->  destroy queue
class ScriptEngine {
public:
    enum class CompileState { Idle, Compiling, Succeeded, Failed };

    // One .NET runtime per process.
    static ScriptEngine& Get();

    // Starts .NET and the managed engine from `managedDir` (default: <exe dir>/Managed).
    // Returns false (see Error()) when .NET or the managed files are missing; the engine then runs without scripts.
    bool Initialize(bool isEditor, const std::filesystem::path& managedDir = {});
    bool IsAvailable() const { return m_Available; }
    const std::string& Error() const { return m_Error; }
    void SetApplicationInfo(const std::filesystem::path& dataPath, const std::string& productName,
                            const std::string& companyName);

    // Where scripts load assets from; without one, Resources.Load finds nothing.
    void SetAssetProvider(ScriptAssetProvider provider);

    // ---- Compilation
    // All *.cs under `assetsDir` (Editor folders excluded, like Unity).
    static std::vector<std::filesystem::path> FindScriptFiles(const std::filesystem::path& assetsDir);
    bool CompileAsync(const std::vector<std::filesystem::path>& sources);
    // Call every frame: finishes a compile (loads the new scripts, logs errors to the console).
    CompileState Poll();
    bool IsCompiling() const { return m_Compiling; }
    bool HasCompileErrors() const { return m_HasErrors; }
    const std::vector<ScriptDiagnostic>& Diagnostics() const { return m_Diagnostics; }
    // Increments whenever a new script assembly is loaded.
    uint64_t Version() const { return m_Version; }
    // [{name, displayName, fullName, fields: [...]}] of the loaded MonoBehaviour classes.
    const nlohmann::json& ScriptTypes() const { return m_Types; }
    const nlohmann::json* FindScriptType(const std::string& name) const;
    // Loads a prebuilt game assembly (players).
    bool LoadAssembly(const std::filesystem::path& dll);

    // ---- Play mode
    bool BeginPlay(Scene& scene, PhysicsWorld* physics);
    void EndPlay();
    bool IsPlaying() const { return m_Playing; }
    void FixedUpdate(float fixedDeltaTime);
    // OnCollision* / OnTrigger* for the contacts of the physics step that just ran.
    void DispatchContacts(const std::vector<ContactEvent>& events);
    void Update(float unscaledDeltaTime);
    void LateUpdate();
    float TimeScale();
    // Application.Quit() was called by a script.
    bool ConsumeQuitRequest();

    // The editor deleted an entity while playing (runs OnDisable / OnDestroy, then removes it).
    void DestroyEntity(EntityID id);
    // Live script instances on an entity: [{class, enabled, fields}].
    nlohmann::json EntityScripts(EntityID id);
    bool SetScriptField(EntityID id, int index, const std::string& field, const nlohmann::json& value);
    bool SetScriptEnabled(EntityID id, int index, bool enabled);
    bool AddScript(EntityID id, const std::string& className, const std::string& fieldsJson = "{}");
    bool RemoveScript(EntityID id, int index);

    // The rectangle (window coordinates) the game is shown in: Input.mousePosition and Screen size
    // are relative to it. Input reaches scripts only while `inputEnabled` (e.g. Game view focused).
    void SetViewport(glm::vec2 origin, glm::vec2 size, bool inputEnabled);

private:
    ScriptEngine() = default;
    void RefreshTypes();
    std::string TakeResult();

    bool m_Initialized = false;
    bool m_Available = false;
    std::string m_Error;
    std::filesystem::path m_ManagedDir;

    bool m_Compiling = false;
    bool m_HasErrors = false;
    std::vector<ScriptDiagnostic> m_Diagnostics;
    uint64_t m_Version = 0;
    nlohmann::json m_Types = nlohmann::json::array();

    bool m_Playing = false;
};

} // namespace ie
