#pragma once

#include <filesystem>
#include <cstdint>
#include <string>

namespace ie {

class Scene;

// Scenes are stored as human-readable JSON (*.zscene).
namespace SceneSerializer {

std::string ToString(const Scene& scene);
bool FromString(Scene& scene, const std::string& text);

bool Save(const Scene& scene, const std::filesystem::path& path);
bool Load(Scene& scene, const std::filesystem::path& path);

// ---- Prefabs (*.zprefab): an entity with all its children.
std::string SubtreeToString(const Scene& scene, uint32_t root);
// Adds the entities to `scene` with fresh ids; the root is parented to `parent`. Returns the root id (0 on error).
uint32_t InstantiateFromString(Scene& scene, const std::string& text, uint32_t parent);
bool SavePrefab(const Scene& scene, uint32_t root, const std::filesystem::path& path);
uint32_t InstantiatePrefab(Scene& scene, const std::filesystem::path& path, uint32_t parent);
std::string ReadTextFile(const std::filesystem::path& path);

} // namespace SceneSerializer
} // namespace ie
