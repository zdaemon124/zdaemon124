#include "ZEngine/Scene/SceneSerializer.h"

#include "ZEngine/Core/Log.h"
#include "ZEngine/Scene/Scene.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>

using json = nlohmann::json;

namespace glm {
// glm <-> json as arrays.
inline void to_json(json& j, const vec2& v) { j = {v.x, v.y}; }
inline void to_json(json& j, const vec3& v) { j = {v.x, v.y, v.z}; }
inline void to_json(json& j, const vec4& v) { j = {v.x, v.y, v.z, v.w}; }
inline void to_json(json& j, const quat& q) { j = {q.x, q.y, q.z, q.w}; }
inline void from_json(const json& j, vec2& v) { v = {j.at(0).get<float>(), j.at(1).get<float>()}; }
inline void from_json(const json& j, vec3& v) { v = {j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>()}; }
inline void from_json(const json& j, vec4& v)
{
    v = {j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>(), j.at(3).get<float>()};
}
inline void from_json(const json& j, quat& q)
{
    q = quat(j.at(3).get<float>(), j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>());
}
} // namespace glm

namespace ze {

NLOHMANN_JSON_SERIALIZE_ENUM(ColliderShape, {
    {ColliderShape::Box, "Box"},
    {ColliderShape::Sphere, "Sphere"},
    {ColliderShape::Capsule, "Capsule"},
})

// Missing fields keep their defaults, so older scene files keep loading.
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Transform, position, rotation, scale)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(MeshRendererComponent, mesh, color, checkerScale)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ColliderComponent, shape, center, size, radius, height, friction,
                                                bounciness, isTrigger)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(RigidbodyComponent, mass, linearDamping, angularDamping, useGravity,
                                                isKinematic)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(LightComponent, color, intensity)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CameraComponent, fieldOfView, nearClip, farClip)
NLOHMANN_JSON_SERIALIZE_ENUM(TextAlign, {
    {TextAlign::Left, "Left"},
    {TextAlign::Center, "Center"},
    {TextAlign::Right, "Right"},
})
NLOHMANN_JSON_SERIALIZE_ENUM(UIScaleMode, {
    {UIScaleMode::ScaleWithScreenSize, "ScaleWithScreenSize"},
    {UIScaleMode::ConstantPixelSize, "ConstantPixelSize"},
})
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(RectTransform, anchorMin, anchorMax, pivot, position, size, order, parent)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(UIImageComponent, sprite, color, preserveAspect)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(UITextComponent, text, fontSize, color, align, shadow)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SceneSettings, skyAmbient, groundAmbient, ambientIntensity, gravity,
                                                uiReferenceResolution, uiScaleMode, uiMatchWidthOrHeight)

namespace SceneSerializer {
namespace {

constexpr int kFormatVersion = 1;

template <class T>
void WriteOptional(json& j, const char* key, const std::optional<T>& value)
{
    if (value)
        j[key] = *value;
}

template <class T>
void ReadOptional(const json& j, const char* key, std::optional<T>& value)
{
    if (auto it = j.find(key); it != j.end())
        value = it->get<T>();
    else
        value.reset();
}

// Scenes saved by v0.3 kept a rect inside UIImage/UIText with a single "anchor".
std::optional<RectTransform> LegacyRect(const json& components)
{
    for (const char* key : {"UIImage", "UIText"}) {
        auto c = components.find(key);
        if (c == components.end() || !c->contains("rect"))
            continue;
        const json& r = (*c)["rect"];
        RectTransform rect;
        auto vec2 = [&](const char* name, glm::vec2 fallback) {
            if (auto it = r.find(name); it != r.end() && it->is_array() && it->size() >= 2)
                return glm::vec2(it->at(0).get<float>(), it->at(1).get<float>());
            return fallback;
        };
        rect.anchorMin = rect.anchorMax = vec2("anchor", {0.5f, 0.5f});
        rect.pivot = vec2("pivot", {0.5f, 0.5f});
        rect.position = vec2("position", {0.0f, 0.0f});
        rect.size = vec2("size", {100.0f, 100.0f});
        rect.order = r.value("order", 0);
        return rect;
    }
    return std::nullopt;
}

} // namespace

std::string ToString(const Scene& scene)
{
    json root;
    root["format"] = "ZEngine Scene";
    root["version"] = kFormatVersion;
    root["settings"] = scene.settings;
    json& entities = root["entities"] = json::array();
    for (const auto& e : scene.Entities()) {
        json je;
        je["id"] = e->id;
        je["name"] = e->name;
        je["active"] = e->active;
        je["transform"] = e->transform;
        json& components = je["components"] = json::object();
        WriteOptional(components, "MeshRenderer", e->meshRenderer);
        WriteOptional(components, "Collider", e->collider);
        WriteOptional(components, "Rigidbody", e->rigidbody);
        WriteOptional(components, "Light", e->light);
        WriteOptional(components, "Camera", e->camera);
        WriteOptional(components, "RectTransform", e->rectTransform);
        WriteOptional(components, "UIImage", e->uiImage);
        WriteOptional(components, "UIText", e->uiText);
        entities.push_back(std::move(je));
    }
    return root.dump(2);
}

bool FromString(Scene& scene, const std::string& text)
{
    json root = json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        Log::Error("Scene file is not valid JSON");
        return false;
    }
    try {
        scene.Clear();
        scene.settings = root.value("settings", SceneSettings{});
        for (const json& je : root.value("entities", json::array())) {
            Entity& e = scene.CreateEntity(je.value("name", std::string("Entity")), je.value("id", 0u));
            e.active = je.value("active", true);
            e.transform = je.value("transform", Transform{});
            const json components = je.value("components", json::object());
            ReadOptional(components, "MeshRenderer", e.meshRenderer);
            ReadOptional(components, "Collider", e.collider);
            ReadOptional(components, "Rigidbody", e.rigidbody);
            ReadOptional(components, "Light", e.light);
            ReadOptional(components, "Camera", e.camera);
            ReadOptional(components, "RectTransform", e.rectTransform);
            ReadOptional(components, "UIImage", e.uiImage);
            ReadOptional(components, "UIText", e.uiText);
            if (!e.rectTransform)
                e.rectTransform = LegacyRect(components);
        }
    } catch (const json::exception& ex) {
        Log::Error("Failed to read scene: {}", ex.what());
        return false;
    }
    return true;
}

bool Save(const Scene& scene, const std::filesystem::path& path)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        Log::Error("Cannot write scene '{}'", path.string());
        return false;
    }
    file << ToString(scene);
    return true;
}

bool Load(Scene& scene, const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        Log::Error("Cannot open scene '{}'", path.string());
        return false;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return FromString(scene, buffer.str());
}

} // namespace SceneSerializer
} // namespace ze
