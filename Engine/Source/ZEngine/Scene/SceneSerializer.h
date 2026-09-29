#pragma once

#include <filesystem>
#include <string>

namespace ze {

class Scene;

// Scenes are stored as human-readable JSON (*.zscene).
namespace SceneSerializer {

std::string ToString(const Scene& scene);
bool FromString(Scene& scene, const std::string& text);

bool Save(const Scene& scene, const std::filesystem::path& path);
bool Load(Scene& scene, const std::filesystem::path& path);

} // namespace SceneSerializer
} // namespace ze
