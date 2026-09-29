#pragma once

#include "IndeetsEngine/Scripting/ScriptEngine.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace ie {

struct ModelAsset;
class UnityAssetDatabase;

// Asset provider for scripts over a project's Assets folder: the engine's own assets (.zprefab,
// models, textures) and Unity ones (.prefab, ScriptableObject .asset, .mat) side by side.
// `loadModel` loads (and caches) models for prefabs that contain them.
ScriptAssetProvider MakeProjectAssets(const std::filesystem::path& assetsDir,
                                      std::function<const ModelAsset*(const std::string& assetPath)> loadModel,
                                      std::shared_ptr<UnityAssetDatabase> database = nullptr);

} // namespace ie
