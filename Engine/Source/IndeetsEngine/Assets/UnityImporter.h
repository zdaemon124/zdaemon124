#pragma once

#include "IndeetsEngine/Assets/UnityYaml.h"
#include "IndeetsEngine/Scene/Scene.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ie {

struct ModelAsset;

// Unity project assets addressed by GUID (from the .meta files next to every asset).
class UnityAssetDatabase {
public:
    explicit UnityAssetDatabase(std::filesystem::path assetsDir);

    const std::filesystem::path& AssetsDir() const { return m_AssetsDir; }
    // Rescans the .meta files (cheap: only their first lines are read).
    void Refresh();

    // Asset path relative to Assets ("" when the GUID is not in the project, e.g. a package asset).
    std::string PathForGuid(const std::string& guid);
    std::string GuidForPath(const std::string& assetPath);
    // Name of a sub-asset (a sprite in a sprite sheet, a mesh in a model) when its .meta lists it.
    std::string SubAssetName(const std::string& guid, int64_t fileId);

    // Parsed documents of a Unity YAML asset (cached until Refresh).
    const std::vector<UnityYaml::Document>& Documents(const std::string& assetPath);

    // Assets inside "Resources" folders: resource path ("Folder/Name", no extension) -> asset path.
    const std::multimap<std::string, std::string>& Resources();

private:
    void EnsureScanned();

    std::filesystem::path m_AssetsDir;
    bool m_Scanned = false;
    std::unordered_map<std::string, std::string> m_PathByGuid;
    std::unordered_map<std::string, std::string> m_GuidByPath;
    std::unordered_map<std::string, std::unordered_map<int64_t, std::string>> m_SubAssets;
    std::unordered_map<std::string, std::vector<UnityYaml::Document>> m_Docs;
    std::multimap<std::string, std::string> m_Resources;
};

struct UnityImportOptions {
    // Loads (and caches) a 3D model; required for model-backed meshes and model prefab instances.
    std::function<const ModelAsset*(const std::string& assetPath)> loadModel;
};

// What an import did; also listed in the console by the editor.
struct UnityImportReport {
    int gameObjects = 0;
    int prefabInstances = 0;
    int modelInstances = 0;
    int scripts = 0;
    int missingScripts = 0;          // package scripts (UI, TMP...) or deleted ones
    std::map<std::string, int> skipped; // unsupported component type -> count
    std::vector<std::string> warnings;
    // Local file id -> entity of the top-level objects of the imported file.
    std::unordered_map<int64_t, EntityID> objects;

    std::string Summary() const;
};

namespace UnityImporter {

bool IsUnityScene(const std::filesystem::path& path);   // .unity
bool IsUnityPrefab(const std::filesystem::path& path);  // .prefab (Unity YAML, not the engine's .zprefab)

// Replaces `scene` with the contents of a .unity scene.
bool ImportScene(UnityAssetDatabase& db, const std::string& assetPath, Scene& scene, const UnityImportOptions& options,
                 UnityImportReport* report = nullptr);

// Adds a Unity prefab (with nested prefabs and models) to the scene. Returns its root entity (0 on failure).
EntityID InstantiatePrefab(UnityAssetDatabase& db, const std::string& assetPath, Scene& scene, EntityID parent,
                           const UnityImportOptions& options, UnityImportReport* report = nullptr);

// A MonoBehaviour-backed asset (ScriptableObject .asset): {"class", "name", "fields"} with fields in the
// engine's script field format. Returns null when the asset is not a script asset.
nlohmann::json LoadScriptAsset(UnityAssetDatabase& db, const std::string& assetPath);

// Base color and texture of a Unity material (.mat).
struct MaterialInfo {
    glm::vec4 color{1.0f};
    std::string texture; // asset path, "" = none
};
MaterialInfo LoadMaterial(UnityAssetDatabase& db, const std::string& assetPath);

} // namespace UnityImporter
} // namespace ie
