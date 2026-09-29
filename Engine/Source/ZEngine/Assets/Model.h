#pragma once

#include "ZEngine/Renderer/MeshData.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace ze {

class Scene;
using EntityID = uint32_t;

// Imported 3D model (FBX / OBJ / glTF / GLB), converted to engine conventions
// (meters, left-handed, +Y up, +Z forward - the same as Unity).
struct ModelAsset {
    struct Node {
        std::string name;
        int parent = -1;
        glm::vec3 position{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 scale{1.0f};
        std::vector<int> parts; // mesh parts drawn by this node
    };
    struct Part {
        std::string name;
        int material = -1;
        uint32_t vertexCount = 0;
        uint32_t triangleCount = 0;
        glm::vec3 boundsMin{0.0f};
        glm::vec3 boundsMax{0.0f};
        bool skinned = false;
    };
    struct Material {
        std::string name;
        glm::vec4 baseColor{1.0f}; // sRGB
        std::string baseColorTexture; // asset path, "" = none
    };
    struct Animation {
        std::string name;
        float duration = 0.0f;
    };

    std::string assetPath;  // path relative to Assets
    std::string format;     // "FBX", "OBJ", "glTF"
    std::vector<Node> nodes;
    std::vector<Part> parts;
    std::vector<MeshData> partData; // geometry, released after the GPU upload
    std::vector<Material> materials;
    std::vector<Animation> animations;
    uint32_t totalVertices = 0;
    uint32_t totalTriangles = 0;
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};

    // Mesh name registered in the renderer for a part.
    std::string PartMeshName(int part) const { return assetPath + "#" + std::to_string(part); }
};

namespace ModelImporter {
bool IsModelFile(const std::filesystem::path& path);
// `assetPath` is relative to `assetRoot`. Embedded textures are extracted next to the model.
std::unique_ptr<ModelAsset> Load(const std::filesystem::path& assetRoot, const std::string& assetPath, std::string& error);
} // namespace ModelImporter

// Creates entities for the model's node hierarchy. Returns the root entity.
EntityID InstantiateModel(Scene& scene, const ModelAsset& model, EntityID parent);

} // namespace ze
