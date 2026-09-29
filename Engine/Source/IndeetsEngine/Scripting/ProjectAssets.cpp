#include "IndeetsEngine/Scripting/ProjectAssets.h"

#include "IndeetsEngine/Assets/Model.h"
#include "IndeetsEngine/Assets/UnityImporter.h"
#include "IndeetsEngine/Core/Log.h"
#include "IndeetsEngine/Core/Platform.h"
#include "IndeetsEngine/Scene/SceneSerializer.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>

namespace ie {
namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

std::string Extension(const std::string& path)
{
    std::string ext = fs::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return ext;
}

bool OneOf(const std::string& ext, std::initializer_list<const char*> list)
{
    return std::any_of(list.begin(), list.end(), [&](const char* e) { return ext == e; });
}

} // namespace

ScriptAssetProvider MakeProjectAssets(const fs::path& assetsDir,
                                      std::function<const ModelAsset*(const std::string& assetPath)> loadModel,
                                      std::shared_ptr<UnityAssetDatabase> database)
{
    auto db = database ? std::move(database) : std::make_shared<UnityAssetDatabase>(assetsDir);
    UnityImportOptions options;
    options.loadModel = loadModel;

    ScriptAssetProvider provider;
    provider.findResources = [db](const std::string& path, bool all) {
        std::vector<std::string> result;
        std::string key = path;
        while (!key.empty() && key.back() == '/')
            key.pop_back();
        for (const auto& [resource, asset] : db->Resources()) {
            bool match = resource == key;
            if (all && !match)
                match = key.empty() || (resource.size() > key.size() && resource.compare(0, key.size(), key) == 0 &&
                                        resource[key.size()] == '/');
            if (match)
                result.push_back(asset);
        }
        return result;
    };

    provider.describe = [db](const std::string& path) {
        std::string ext = Extension(path);
        json info{{"name", Platform::PathToUtf8(fs::path(Platform::Utf8ToPath(path)).stem())}};
        std::error_code ec;
        if (!fs::exists(db->AssetsDir() / Platform::Utf8ToPath(path), ec)) {
            info["kind"] = "missing";
        } else if (ext == ".asset") {
            json so = UnityImporter::LoadScriptAsset(*db, path);
            if (so.is_object()) {
                info = so;
                info["kind"] = "script";
            } else {
                info["kind"] = "other";
            }
        } else if (ext == ".prefab" || ext == ".zprefab") {
            info["kind"] = "prefab";
        } else if (ModelImporter::IsModelFile(Platform::Utf8ToPath(path))) {
            info["kind"] = "model";
        } else if (OneOf(ext, {".png", ".jpg", ".jpeg", ".tga", ".bmp", ".psd", ".tif", ".tiff", ".gif", ".exr", ".hdr"})) {
            info["kind"] = "texture";
        } else if (ext == ".mat") {
            UnityImporter::MaterialInfo m = UnityImporter::LoadMaterial(*db, path);
            info["kind"] = "material";
            info["color"] = {m.color.r, m.color.g, m.color.b, m.color.a};
            info["texture"] = m.texture;
        } else if (OneOf(ext, {".wav", ".mp3", ".ogg", ".aif", ".aiff", ".flac"})) {
            info["kind"] = "audio";
        } else if (OneOf(ext, {".txt", ".json", ".bytes", ".csv", ".xml", ".html", ".htm", ".yaml", ".md"})) {
            info["kind"] = "text";
        } else {
            info["kind"] = "other";
        }
        return info;
    };

    provider.instantiate = [db, options](Scene& scene, const std::string& path, EntityID parent) -> EntityID {
        std::string ext = Extension(path);
        if (ext == ".zprefab")
            return SceneSerializer::InstantiatePrefab(scene, db->AssetsDir() / Platform::Utf8ToPath(path), parent);
        if (ext == ".prefab") {
            UnityImportReport report;
            EntityID root = UnityImporter::InstantiatePrefab(*db, path, scene, parent, options, &report);
            if (!root)
                Log::Warn("Prefab '{}' could not be loaded", path);
            return root;
        }
        if (ModelImporter::IsModelFile(Platform::Utf8ToPath(path)) && options.loadModel)
            if (const ModelAsset* model = options.loadModel(path))
                return InstantiateModel(scene, *model, parent);
        return 0;
    };
    return provider;
}

} // namespace ie
