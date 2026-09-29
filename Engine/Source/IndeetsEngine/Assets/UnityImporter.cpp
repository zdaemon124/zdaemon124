#include "IndeetsEngine/Assets/UnityImporter.h"

#include "IndeetsEngine/Assets/Model.h"
#include "IndeetsEngine/Core/Log.h"
#include "IndeetsEngine/Core/Platform.h"
#include "IndeetsEngine/Scene/Primitives.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <unordered_set>

namespace ie {

namespace fs = std::filesystem;
using json = nlohmann::json;
using UnityYaml::Document;

// json::value() throws on anything but an object; Unity files have empty bodies and null entries.
static json Get(const json& j, const std::string& key, json fallback = json())
{
    if (j.is_object())
        if (auto it = j.find(key); it != j.end() && !it->is_null())
            return *it;
    return fallback;
}

namespace {

std::string Lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

bool IsImagePath(const std::string& path)
{
    std::string ext = Lower(fs::path(path).extension().string());
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp";
}

bool IsModelPath(const std::string& path)
{
    std::string ext = Lower(fs::path(path).extension().string());
    return ext == ".fbx" || ext == ".obj" || ext == ".gltf" || ext == ".glb";
}

} // namespace

// ============================================================================ asset database

UnityAssetDatabase::UnityAssetDatabase(fs::path assetsDir) : m_AssetsDir(std::move(assetsDir)) {}

void UnityAssetDatabase::Refresh()
{
    m_Scanned = false;
    m_PathByGuid.clear();
    m_GuidByPath.clear();
    m_SubAssets.clear();
    m_Docs.clear();
    m_Resources.clear();
}

void UnityAssetDatabase::EnsureScanned()
{
    if (m_Scanned)
        return;
    m_Scanned = true;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(m_AssetsDir, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec)
            break;
        const fs::path& p = it->path();
        if (!it->is_regular_file(ec))
            continue;
        std::string rel = Platform::PathToUtf8(fs::relative(p, m_AssetsDir, ec));
        if (p.extension() != ".meta") {
            // Resources/...: Resources.Load finds assets by their path below the folder, without extension.
            size_t r = rel.find("Resources/");
            if (r == 0 || (r != std::string::npos && rel[r - 1] == '/')) {
                std::string resource = rel.substr(r + 10);
                size_t dot = resource.rfind('.');
                if (dot != std::string::npos && resource.find('/', dot) == std::string::npos)
                    resource.resize(dot);
                m_Resources.emplace(resource, rel);
            }
            continue;
        }
        std::ifstream file(p);
        std::string line;
        for (int i = 0; i < 4 && std::getline(file, line); ++i) {
            if (line.rfind("guid: ", 0) == 0) {
                std::string guid = line.substr(6);
                while (!guid.empty() && (guid.back() == '\r' || guid.back() == ' '))
                    guid.pop_back();
                std::string asset = rel.substr(0, rel.size() - 5); // "x.png.meta" -> "x.png"
                m_PathByGuid[guid] = asset;
                m_GuidByPath[asset] = guid;
                break;
            }
        }
    }
}

std::string UnityAssetDatabase::PathForGuid(const std::string& guid)
{
    EnsureScanned();
    auto it = m_PathByGuid.find(guid);
    return it != m_PathByGuid.end() ? it->second : std::string();
}

std::string UnityAssetDatabase::GuidForPath(const std::string& assetPath)
{
    EnsureScanned();
    auto it = m_GuidByPath.find(assetPath);
    return it != m_GuidByPath.end() ? it->second : std::string();
}

const std::multimap<std::string, std::string>& UnityAssetDatabase::Resources()
{
    EnsureScanned();
    return m_Resources;
}

std::string UnityAssetDatabase::SubAssetName(const std::string& guid, int64_t fileId)
{
    auto cached = m_SubAssets.find(guid);
    if (cached == m_SubAssets.end()) {
        std::unordered_map<int64_t, std::string> names;
        std::string path = PathForGuid(guid);
        if (!path.empty()) {
            std::vector<Document> meta = UnityYaml::ParseFile(m_AssetsDir / Platform::Utf8ToPath(path + ".meta"));
            if (!meta.empty()) {
                const json& root = meta[0].body;
                // Importer settings are the single top-level key (TextureImporter, ModelImporter...).
                for (const auto& [importer, settings] : root.items()) {
                    if (!settings.is_object())
                        continue;
                    for (const json& entry : Get(settings, "internalIDToNameTable", json::array()))
                        if (entry.contains("first") && entry["first"].is_object())
                            for (const auto& [cls, id] : entry["first"].items())
                                names[UnityYaml::Integer(id)] = UnityYaml::String(Get(entry, "second", json("")));
                    if (settings.contains("spriteSheet") && settings["spriteSheet"].is_object())
                        for (const json& sprite : Get(settings["spriteSheet"], "sprites", json::array()))
                            names[UnityYaml::Integer(Get(sprite, "internalID", json("0")))] =
                                UnityYaml::String(Get(sprite, "name", json("")));
                }
            }
        }
        cached = m_SubAssets.emplace(guid, std::move(names)).first;
    }
    auto it = cached->second.find(fileId);
    return it != cached->second.end() ? it->second : std::string();
}

const std::vector<Document>& UnityAssetDatabase::Documents(const std::string& assetPath)
{
    auto it = m_Docs.find(assetPath);
    if (it == m_Docs.end())
        it = m_Docs.emplace(assetPath, UnityYaml::ParseFile(m_AssetsDir / Platform::Utf8ToPath(assetPath))).first;
    return it->second;
}

std::string UnityImportReport::Summary() const
{
    std::string text = std::format("{} objects, {} prefab instances, {} model instances, {} scripts", gameObjects,
                                   prefabInstances, modelInstances, scripts);
    if (missingScripts)
        text += std::format(", {} scripts not in the project (packages)", missingScripts);
    if (!skipped.empty()) {
        text += "; not supported yet:";
        for (const auto& [type, count] : skipped)
            text += std::format(" {} x{}", type, count);
    }
    return text;
}

// ============================================================================ importer

namespace {

enum class Kind {
    GameObject, Transform, MeshFilter, MeshRenderer, SkinnedMeshRenderer, BoxCollider, SphereCollider,
    CapsuleCollider, MeshCollider, CharacterController, Rigidbody, Light, Camera, Script, Other
};

Kind KindOf(int classId)
{
    switch (classId) {
    case 1: return Kind::GameObject;
    case 4: case 224: return Kind::Transform;
    case 33: return Kind::MeshFilter;
    case 23: return Kind::MeshRenderer;
    case 137: return Kind::SkinnedMeshRenderer;
    case 65: return Kind::BoxCollider;
    case 135: return Kind::SphereCollider;
    case 136: return Kind::CapsuleCollider;
    case 64: return Kind::MeshCollider;
    case 143: return Kind::CharacterController;
    case 54: return Kind::Rigidbody;
    case 108: return Kind::Light;
    case 20: return Kind::Camera;
    case 114: return Kind::Script;
    default: return Kind::Other;
    }
}

const char* kBuiltinGuid = "0000000000000000e000000000000000";

std::string BuiltinMesh(int64_t fileId)
{
    switch (fileId) {
    case 10202: return PrimitiveName(PrimitiveType::Cube);
    case 10206: return PrimitiveName(PrimitiveType::Cylinder);
    case 10207: return PrimitiveName(PrimitiveType::Sphere);
    case 10208: return PrimitiveName(PrimitiveType::Capsule);
    case 10209: return PrimitiveName(PrimitiveType::Plane);
    case 10210: return PrimitiveName(PrimitiveType::Quad);
    default: return "";
    }
}

float F(const json& v, const char* key, float fallback = 0.0f)
{
    auto it = v.find(key);
    return it != v.end() ? float(UnityYaml::Number(*it, fallback)) : fallback;
}

bool B(const json& v, const char* key, bool fallback)
{
    auto it = v.find(key);
    return it != v.end() ? UnityYaml::Integer(*it, fallback ? 1 : 0) != 0 : fallback;
}

glm::vec3 V3(const json& v, const char* key, glm::vec3 fallback)
{
    auto it = v.find(key);
    if (it == v.end() || !it->is_object())
        return fallback;
    return {F(*it, "x", fallback.x), F(*it, "y", fallback.y), F(*it, "z", fallback.z)};
}

glm::quat Q(const json& v, const char* key)
{
    auto it = v.find(key);
    if (it == v.end() || !it->is_object())
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::quat q(F(*it, "w", 1.0f), F(*it, "x"), F(*it, "y"), F(*it, "z"));
    float len = glm::length(q);
    return len > 1e-6f ? q / len : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
}

glm::vec4 Color(const json& v, glm::vec4 fallback = glm::vec4(1.0f))
{
    if (!v.is_object())
        return fallback;
    return {F(v, "r", fallback.r), F(v, "g", fallback.g), F(v, "b", fallback.b), F(v, "a", fallback.a)};
}

int64_t RefFileId(const json& ref) { return ref.is_object() ? UnityYaml::Integer(Get(ref, "fileID", json("0"))) : 0; }
std::string RefGuid(const json& ref) { return ref.is_object() ? UnityYaml::String(Get(ref, "guid", json(""))) : ""; }

// Keys every MonoBehaviour has that are not script fields.
bool IsHeaderKey(const std::string& key)
{
    static const std::unordered_set<std::string> keys = {
        "m_ObjectHideFlags", "m_CorrespondingSourceObject", "m_PrefabInstance", "m_PrefabAsset", "m_GameObject",
        "m_Enabled", "m_EditorHideFlags", "m_Script", "m_Name", "m_EditorClassIdentifier", "serializedVersion"};
    return keys.contains(key);
}

// A component created on an entity, so prefab modifications and removals can find it again.
struct Slot {
    EntityID entity = 0;
    Kind kind = Kind::Other;
    int script = -1; // index into Entity::scripts
};

// Mesh and materials of a renderer, resolved once all components of the file exist.
struct PendingRenderer {
    json mesh;           // MeshFilter / SkinnedMeshRenderer m_Mesh
    json materials = json::array();
    bool enabled = true;
    bool hasRenderer = false;
    bool meshCollider = false;
    json colliderMesh;
    bool colliderTrigger = false;
};

class Builder {
public:
    Builder(UnityAssetDatabase& db, Scene& scene, const UnityImportOptions& options, UnityImportReport& report)
        : m_Db(db), m_Scene(scene), m_Options(options), m_Report(report)
    {
    }

    // Objects of one built file, by the file ids they have there.
    struct FileInstance {
        std::unordered_map<int64_t, EntityID> entities; // GameObject / Transform / component id -> entity
        std::unordered_map<int64_t, Slot> slots;         // component id -> component
        EntityID root = 0;
        bool model = false;
        int context = -1;
    };

    FileInstance BuildFile(const std::string& assetPath, EntityID parent, int depth)
    {
        FileInstance inst;
        if (depth > 16) {
            Warn("Prefab nesting too deep at " + assetPath);
            return inst;
        }
        const std::vector<Document>& docs = m_Db.Documents(assetPath);
        inst.context = int(m_Contexts.size());
        m_Contexts.emplace_back();
        std::unordered_map<int64_t, const Document*> byId;
        for (const Document& d : docs)
            byId[d.fileId] = &d;

        // 1. Nested prefab / model instances.
        std::vector<std::pair<const Document*, FileInstance>> nested;
        for (const Document& d : docs) {
            if (d.classId != 1001)
                continue;
            FileInstance child = BuildInstance(d, depth);
            if (!child.root)
                continue;
            // Objects inside a nested instance are addressed as (instance id XOR source id).
            for (const auto& [sourceId, entity] : child.entities) {
                inst.entities[d.fileId ^ sourceId] = entity;
                inst.entities[(d.fileId ^ sourceId) & 0x7fffffffffffffffLL] = entity;
            }
            for (const auto& [sourceId, slot] : child.slots) {
                inst.slots[d.fileId ^ sourceId] = slot;
                inst.slots[(d.fileId ^ sourceId) & 0x7fffffffffffffffLL] = slot;
            }
            nested.emplace_back(&d, std::move(child));
        }
        std::unordered_map<int64_t, const FileInstance*> nestedById;
        for (const auto& [doc, child] : nested)
            nestedById[doc->fileId] = &child;

        // Stripped objects stand for objects of a nested instance.
        for (const Document& d : docs) {
            if (!d.stripped)
                continue;
            int64_t pi = RefFileId(Get(d.body, "m_PrefabInstance", json()));
            int64_t source = RefFileId(Get(d.body, "m_CorrespondingSourceObject", json()));
            auto n = nestedById.find(pi);
            if (n == nestedById.end())
                continue;
            const FileInstance& child = *n->second;
            auto e = child.entities.find(source);
            EntityID entity = e != child.entities.end() ? e->second : child.root;
            inst.entities[d.fileId] = entity;
            if (auto s = child.slots.find(source); s != child.slots.end())
                inst.slots[d.fileId] = s->second;
        }
        m_Contexts[size_t(inst.context)] = inst.entities; // for references while applying modifications

        // 2. Local GameObjects.
        for (const Document& d : docs) {
            if (d.classId != 1 || d.stripped)
                continue;
            Entity& e = m_Scene.CreateEntity(UnityYaml::String(Get(d.body, "m_Name", json("GameObject"))));
            e.active = B(d.body, "m_IsActive", true);
            e.layer = int(UnityYaml::Integer(Get(d.body, "m_Layer", json("0"))));
            e.tag = UnityYaml::String(Get(d.body, "m_TagString", json("Untagged")));
            if (e.tag.empty())
                e.tag = "Untagged";
            e.parent = parent;
            inst.entities[d.fileId] = e.id;
            ++m_Report.gameObjects;
        }

        // 3. Local components.
        for (const Document& d : docs) {
            if (d.stripped || d.classId == 1 || d.classId == 1001)
                continue;
            auto owner = inst.entities.find(RefFileId(Get(d.body, "m_GameObject", json())));
            if (owner == inst.entities.end())
                continue; // file-level objects (RenderSettings...) or broken references
            Entity* e = m_Scene.Get(owner->second);
            if (!e)
                continue;
            inst.entities[d.fileId] = e->id;
            Slot slot = AddComponent(*e, d, inst.context);
            if (slot.kind != Kind::Other)
                inst.slots[d.fileId] = slot;
        }
        m_Contexts[size_t(inst.context)] = inst.entities;

        // 4. Modifications of the nested instances (they may reference local objects, so after 2-3).
        for (auto& [doc, child] : nested)
            ApplyModifications(*doc, child, inst.context);

        // 5. Hierarchy.
        std::vector<std::pair<EntityID, std::vector<EntityID>>> childOrder;
        for (const Document& d : docs) {
            if (d.stripped || KindOf(d.classId) != Kind::Transform)
                continue;
            Entity* e = m_Scene.Get(inst.entities[d.fileId]);
            if (!e)
                continue;
            int64_t father = RefFileId(Get(d.body, "m_Father", json()));
            if (father) {
                auto p = inst.entities.find(father);
                e->parent = p != inst.entities.end() ? p->second : parent;
            } else {
                e->parent = parent;
                if (!inst.root)
                    inst.root = e->id;
            }
            std::vector<EntityID> order;
            for (const json& c : Get(d.body, "m_Children", json::array())) {
                auto ce = inst.entities.find(RefFileId(c));
                if (ce != inst.entities.end())
                    order.push_back(ce->second);
            }
            if (order.size() > 1)
                childOrder.emplace_back(e->id, std::move(order));
        }
        for (auto& [doc, child] : nested) {
            const json& mod = Get(doc->body, "m_Modification", json::object());
            int64_t tp = RefFileId(Get(mod, "m_TransformParent", json()));
            Entity* root = m_Scene.Get(child.root);
            if (!root)
                continue;
            auto p = tp ? inst.entities.find(tp) : inst.entities.end();
            root->parent = p != inst.entities.end() ? p->second : parent;
            if (!tp && !inst.root)
                inst.root = child.root; // prefab variant: the file's root is an instance
        }
        m_ChildOrder.insert(m_ChildOrder.end(), childOrder.begin(), childOrder.end());

        // 6. Renderers and mesh colliders need the whole file (MeshFilter + MeshRenderer).
        FinishRenderers();

        m_Contexts[size_t(inst.context)] = inst.entities;
        m_Report.objects = inst.entities;
        return inst;
    }

    // Replaces {"__local": id, "__ctx": n} markers left in script fields with entity references.
    void ResolveReferences(const std::vector<EntityID>& created)
    {
        for (EntityID id : created) {
            Entity* e = m_Scene.Get(id);
            if (!e)
                continue;
            for (ScriptComponent& s : e->scripts) {
                if (s.fields.find("__local") == std::string::npos)
                    continue;
                json fields = json::parse(s.fields, nullptr, false);
                ResolveNode(fields);
                s.fields = fields.dump();
            }
            std::erase_if(e->scripts, [](const ScriptComponent& s) { return s.className.empty(); });
        }
    }

    // Scene root order (SceneRoots, Unity 2022+): the order of the top-level objects in the Hierarchy.
    void AddRootOrder(const std::string& assetPath)
    {
        for (const Document& d : m_Db.Documents(assetPath)) {
            if (d.classId != 1660057539)
                continue;
            std::vector<EntityID> order;
            for (const json& root : Get(d.body, "m_Roots", json::array()))
                if (auto it = m_Report.objects.find(RefFileId(root)); it != m_Report.objects.end())
                    order.push_back(it->second);
            m_ChildOrder.emplace_back(EntityID(0), std::move(order));
        }
    }

    void ApplyChildOrder()
    {
        for (const auto& [parentId, order] : m_ChildOrder) {
            // Keep the storage slots the children already occupy, but fill them in Unity's order.
            std::vector<int> slots;
            for (EntityID c : order)
                if (int i = m_Scene.IndexOf(c); i >= 0)
                    slots.push_back(i);
            std::sort(slots.begin(), slots.end());
            for (size_t k = 0; k < order.size() && k < slots.size(); ++k)
                m_Scene.Move(order[k], slots[k]);
        }
    }

    void ApplyRenderSettings(const std::string& assetPath)
    {
        for (const Document& d : m_Db.Documents(assetPath)) {
            if (d.classId != 104)
                continue;
            glm::vec4 sky = Color(Get(d.body, "m_AmbientSkyColor", json()), glm::vec4(0.45f, 0.52f, 0.62f, 1.0f));
            glm::vec4 ground = Color(Get(d.body, "m_AmbientGroundColor", json()), glm::vec4(0.32f, 0.3f, 0.28f, 1.0f));
            m_Scene.settings.skyAmbient = glm::vec3(sky);
            m_Scene.settings.groundAmbient = glm::vec3(ground);
            m_Scene.settings.ambientIntensity = F(d.body, "m_AmbientIntensity", 1.0f);
        }
    }

    // Script fields of a MonoBehaviour body outside any scene (asset files): no local references.
    json ConvertFields(const json& body)
    {
        json fields = json::object();
        for (const auto& [key, value] : body.items())
            if (!IsHeaderKey(key))
                fields[key] = ConvertValue(value, -1);
        return fields;
    }

    void Warn(const std::string& text)
    {
        if (m_Report.warnings.size() < 200)
            m_Report.warnings.push_back(text);
    }

private:
    FileInstance BuildInstance(const Document& d, int depth)
    {
        FileInstance child;
        json source = Get(d.body, "m_SourcePrefab", json());
        std::string guid = RefGuid(source);
        std::string path = m_Db.PathForGuid(guid);
        if (path.empty()) {
            Warn("Prefab instance source not in the project: " + guid);
            return child;
        }
        if (IsModelPath(path)) {
            const ModelAsset* model = m_Options.loadModel ? m_Options.loadModel(path) : nullptr;
            if (!model) {
                Warn("Model could not be loaded: " + path);
                return child;
            }
            child.root = InstantiateModel(m_Scene, *model, 0);
            child.model = true;
            ++m_Report.modelInstances;
            return child;
        }
        std::string ext = Lower(fs::path(path).extension().string());
        if (ext != ".prefab") {
            Warn("Unsupported prefab instance source: " + path);
            return child;
        }
        ++m_Report.prefabInstances;
        return BuildFile(path, 0, depth + 1);
    }

    // ---- components

    Slot AddComponent(Entity& e, const Document& d, int context)
    {
        Slot slot{e.id, KindOf(d.classId), -1};
        const json& b = d.body;
        bool enabled = B(b, "m_Enabled", true);
        switch (slot.kind) {
        case Kind::Transform:
            e.transform.position = V3(b, "m_LocalPosition", glm::vec3(0.0f));
            e.transform.rotation = Q(b, "m_LocalRotation");
            e.transform.scale = V3(b, "m_LocalScale", glm::vec3(1.0f));
            break;
        case Kind::MeshFilter:
            Pending(e.id).mesh = Get(b, "m_Mesh", json());
            break;
        case Kind::MeshRenderer:
            Pending(e.id).hasRenderer = true;
            Pending(e.id).enabled = enabled;
            Pending(e.id).materials = Get(b, "m_Materials", json::array());
            break;
        case Kind::SkinnedMeshRenderer:
            Pending(e.id).hasRenderer = true;
            Pending(e.id).enabled = enabled;
            Pending(e.id).mesh = Get(b, "m_Mesh", json());
            Pending(e.id).materials = Get(b, "m_Materials", json::array());
            break;
        case Kind::BoxCollider:
        case Kind::SphereCollider:
        case Kind::CapsuleCollider:
        case Kind::CharacterController: {
            if (!enabled)
                break;
            ColliderComponent c;
            c.center = V3(b, "m_Center", glm::vec3(0.0f));
            c.isTrigger = B(b, "m_IsTrigger", false);
            if (slot.kind == Kind::BoxCollider) {
                c.shape = ColliderShape::Box;
                c.size = V3(b, "m_Size", glm::vec3(1.0f));
            } else if (slot.kind == Kind::SphereCollider) {
                c.shape = ColliderShape::Sphere;
                c.radius = F(b, "m_Radius", 0.5f);
            } else {
                c.shape = ColliderShape::Capsule;
                c.radius = F(b, "m_Radius", 0.5f);
                c.height = F(b, "m_Height", 2.0f);
            }
            e.collider = c;
            break;
        }
        case Kind::MeshCollider:
            if (enabled) {
                Pending(e.id).meshCollider = true;
                Pending(e.id).colliderMesh = Get(b, "m_Mesh", json());
                Pending(e.id).colliderTrigger = B(b, "m_IsTrigger", false);
            }
            break;
        case Kind::Rigidbody: {
            RigidbodyComponent rb;
            rb.mass = F(b, "m_Mass", 1.0f);
            rb.linearDamping = b.contains("m_LinearDamping") ? F(b, "m_LinearDamping") : F(b, "m_Drag", 0.0f);
            rb.angularDamping = b.contains("m_AngularDamping") ? F(b, "m_AngularDamping") : F(b, "m_AngularDrag", 0.05f);
            rb.useGravity = B(b, "m_UseGravity", true);
            rb.isKinematic = B(b, "m_IsKinematic", false);
            e.rigidbody = rb;
            if (!e.collider)
                m_NeedsCollider.push_back(e.id);
            break;
        }
        case Kind::Light: {
            int type = int(UnityYaml::Integer(Get(b, "m_Type", json("1"))));
            if (type != 1 || !enabled) {
                Skip(type == 1 ? "Light (disabled)" : "Light (point/spot/area)");
                slot.kind = Kind::Other;
                break;
            }
            LightComponent l;
            l.color = glm::vec3(Color(Get(b, "m_Color", json())));
            l.intensity = F(b, "m_Intensity", 1.0f);
            e.light = l;
            break;
        }
        case Kind::Camera: {
            CameraComponent c;
            c.fieldOfView = F(b, "field of view", 60.0f);
            c.nearClip = F(b, "near clip plane", 0.3f);
            c.farClip = F(b, "far clip plane", 1000.0f);
            if (enabled)
                e.camera = c;
            break;
        }
        case Kind::Script: {
            std::string className = ScriptClass(Get(b, "m_Script", json()));
            if (className.empty()) {
                ++m_Report.missingScripts;
                slot.kind = Kind::Other;
                break;
            }
            ScriptComponent s;
            s.className = className;
            s.enabled = enabled;
            json fields = json::object();
            for (const auto& [key, value] : b.items())
                if (!IsHeaderKey(key))
                    fields[key] = ConvertValue(value, context);
            s.fields = fields.dump();
            e.scripts.push_back(std::move(s));
            slot.script = int(e.scripts.size()) - 1;
            ++m_Report.scripts;
            break;
        }
        case Kind::GameObject:
        case Kind::Other:
            Skip(d.type.empty() ? std::format("class {}", d.classId) : d.type);
            break;
        }
        return slot;
    }

    void Skip(const std::string& type) { ++m_Report.skipped[type]; }

    PendingRenderer& Pending(EntityID id) { return m_Pending[id]; }

    std::string ScriptClass(const json& scriptRef)
    {
        std::string path = m_Db.PathForGuid(RefGuid(scriptRef));
        if (path.empty() || Lower(fs::path(path).extension().string()) != ".cs")
            return "";
        return Platform::Utf8ToPath(path).stem().string(); // Unity: the class is named like its file
    }

    // Mesh name for a mesh reference; `ownerName` picks the model node (Unity names meshes after nodes).
    // Returns the parts of the node (several when it has several materials).
    std::vector<std::string> ResolveMesh(const json& meshRef, const std::string& ownerName, std::vector<glm::vec3>* bounds = nullptr)
    {
        std::string guid = RefGuid(meshRef);
        int64_t fileId = RefFileId(meshRef);
        if (guid.empty() || fileId == 0)
            return {};
        if (guid == kBuiltinGuid) {
            std::string builtin = BuiltinMesh(fileId);
            if (bounds && !builtin.empty()) {
                bounds->push_back(glm::vec3(-0.5f));
                bounds->push_back(glm::vec3(0.5f));
            }
            return builtin.empty() ? std::vector<std::string>{} : std::vector<std::string>{builtin};
        }
        std::string path = m_Db.PathForGuid(guid);
        if (path.empty() || !IsModelPath(path))
            return {};
        const ModelAsset* model = m_Options.loadModel ? m_Options.loadModel(path) : nullptr;
        if (!model)
            return {};
        std::string subName = m_Db.SubAssetName(guid, fileId);
        auto pick = [&](const std::string& name) -> int {
            for (size_t i = 0; i < model->nodes.size(); ++i)
                if (!model->nodes[i].parts.empty() && model->nodes[i].name == name)
                    return int(i);
            return -1;
        };
        int node = subName.empty() ? -1 : pick(subName);
        if (node < 0)
            node = pick(ownerName);
        if (node < 0) {
            int withParts = 0, last = -1;
            for (size_t i = 0; i < model->nodes.size(); ++i)
                if (!model->nodes[i].parts.empty()) {
                    ++withParts;
                    if (last < 0)
                        last = int(i);
                }
            node = last;
            if (withParts > 1)
                Warn(std::format("Mesh of '{}' in {} matched by guess (the model has {} meshes)", ownerName, path, withParts));
        }
        if (node < 0)
            return {};
        std::vector<std::string> names;
        for (int part : model->nodes[size_t(node)].parts) {
            names.push_back(model->PartMeshName(part));
            if (bounds) {
                bounds->push_back(model->parts[size_t(part)].boundsMin);
                bounds->push_back(model->parts[size_t(part)].boundsMax);
            }
        }
        return names;
    }

    void FinishRenderers()
    {
        for (auto& [id, p] : m_Pending) {
            Entity* e = m_Scene.Get(id);
            if (!e)
                continue;
            std::vector<glm::vec3> bounds;
            if (p.hasRenderer && p.enabled && !p.mesh.is_null()) {
                std::vector<std::string> meshes = ResolveMesh(p.mesh, e->name, &bounds);
                for (size_t i = 0; i < meshes.size(); ++i) {
                    Entity* target = e;
                    if (i > 0) {
                        // Extra material slots of a model mesh become children, as in the model importer.
                        Entity& sub = m_Scene.CreateEntity(e->name + " (" + std::to_string(i) + ")");
                        sub.parent = id;
                        e = m_Scene.Get(id);
                        target = &sub;
                    }
                    target->meshRenderer = MeshRendererComponent{};
                    target->meshRenderer->mesh = meshes[i];
                    const json& mat = i < p.materials.size() ? p.materials[i]
                                      : p.materials.empty()   ? json()
                                                              : p.materials.back();
                    ApplyMaterial(*target->meshRenderer, mat);
                }
            }
            if (p.meshCollider && !e->collider) {
                // No mesh colliders yet: a box around the mesh keeps floors and walls solid.
                std::vector<glm::vec3> cb;
                const json& ref = p.colliderMesh.is_null() ? p.mesh : p.colliderMesh;
                ResolveMesh(ref, e->name, &cb);
                if (cb.size() >= 2) {
                    glm::vec3 mn = cb[0], mx = cb[1];
                    for (size_t i = 2; i + 1 < cb.size(); i += 2) {
                        mn = glm::min(mn, cb[i]);
                        mx = glm::max(mx, cb[i + 1]);
                    }
                    ColliderComponent c;
                    c.center = (mn + mx) * 0.5f;
                    c.size = glm::max(mx - mn, glm::vec3(0.02f));
                    c.isTrigger = p.colliderTrigger;
                    e->collider = c;
                    ++m_Report.skipped["MeshCollider (as box)"];
                }
            }
        }
        m_Pending.clear();
        for (EntityID id : m_NeedsCollider)
            if (Entity* e = m_Scene.Get(id); e && !e->collider)
                Skip("Rigidbody without a collider");
        m_NeedsCollider.clear();
    }

    void ApplyMaterial(MeshRendererComponent& mr, const json& ref)
    {
        std::string path = m_Db.PathForGuid(RefGuid(ref));
        if (path.empty() || Lower(fs::path(path).extension().string()) != ".mat")
            return;
        UnityImporter::MaterialInfo info = UnityImporter::LoadMaterial(m_Db, path);
        mr.color = info.color;
        mr.texture = info.texture;
    }

    // ---- script field values

    json ConvertRef(const json& ref, int context)
    {
        int64_t fileId = RefFileId(ref);
        if (fileId == 0)
            return nullptr;
        std::string guid = RefGuid(ref);
        if (guid.empty())
            return context >= 0 ? json{{"__local", fileId}, {"__ctx", context}} : json(nullptr);
        std::string path = m_Db.PathForGuid(guid);
        if (path.empty())
            return nullptr; // package or built-in asset
        json asset{{"asset", path}, {"fileID", fileId}};
        if (std::string sub = m_Db.SubAssetName(guid, fileId); !sub.empty())
            asset["name"] = sub;
        return asset;
    }

    static bool HasOnly(const json& obj, std::initializer_list<const char*> keys, size_t minimum)
    {
        size_t matched = 0;
        for (const auto& [k, v] : obj.items()) {
            if (k == "serializedVersion")
                continue;
            bool known = false;
            for (const char* key : keys)
                known |= k == key;
            if (!known || v.is_object() || v.is_array())
                return false;
            ++matched;
        }
        return matched >= minimum;
    }

    json ConvertValue(const json& v, int context)
    {
        if (v.is_array()) {
            json out = json::array();
            for (const json& item : v)
                out.push_back(ConvertValue(item, context));
            return out;
        }
        if (!v.is_object())
            return v;
        if (v.contains("fileID") && (v.size() == 1 || v.contains("guid")))
            return ConvertRef(v, context);
        auto num = [&](const char* k) { return UnityYaml::Number(Get(v, k, json("0"))); };
        if (HasOnly(v, {"x", "y", "z", "w"}, 2)) {
            json a = json::array({num("x"), num("y")});
            if (v.contains("z")) a.push_back(num("z"));
            if (v.contains("w")) a.push_back(num("w"));
            return a;
        }
        if (HasOnly(v, {"r", "g", "b", "a"}, 3))
            return json::array({num("r"), num("g"), num("b"), v.contains("a") ? num("a") : 1.0});
        if (HasOnly(v, {"x", "y", "width", "height"}, 4))
            return json::array({num("x"), num("y"), num("width"), num("height")});
        if (HasOnly(v, {"m_Bits"}, 1))
            return num("m_Bits");
        if (v.contains("m_Center") && v.contains("m_Extent")) {
            glm::vec3 c = V3(v, "m_Center", glm::vec3(0.0f)), ex = V3(v, "m_Extent", glm::vec3(0.0f));
            return json::array({c.x, c.y, c.z, ex.x * 2.0f, ex.y * 2.0f, ex.z * 2.0f});
        }
        json out = json::object();
        for (const auto& [k, item] : v.items())
            if (k != "serializedVersion")
                out[k] = ConvertValue(item, context);
        return out;
    }

    void ResolveNode(json& node)
    {
        if (node.is_object()) {
            if (node.contains("__local")) {
                int ctx = node.value("__ctx", -1);
                int64_t id = node["__local"].get<int64_t>();
                EntityID entity = 0;
                if (ctx >= 0 && size_t(ctx) < m_Contexts.size())
                    if (auto it = m_Contexts[size_t(ctx)].find(id); it != m_Contexts[size_t(ctx)].end())
                        entity = it->second;
                node = entity ? json{{"entity", entity}} : json(nullptr);
                return;
            }
            for (auto& [k, child] : node.items())
                ResolveNode(child);
        } else if (node.is_array()) {
            for (json& child : node)
                ResolveNode(child);
        }
    }

    // ---- prefab instance modifications

    static bool IsTransformProperty(const std::string& p)
    {
        return p.rfind("m_LocalPosition.", 0) == 0 || p.rfind("m_LocalRotation.", 0) == 0 ||
               p.rfind("m_LocalScale.", 0) == 0;
    }

    void ApplyModifications(const Document& d, FileInstance& child, int context)
    {
        const json& mod = Get(d.body, "m_Modification", json::object());
        // Model instances: Unity addresses the model's objects by hashes we cannot reproduce, so
        // modifications go to the root (the usual case: placing the model in the scene).
        int64_t modelRootTransform = 0, modelRootObject = 0;
        std::unordered_set<int64_t> removed;
        for (const json& r : Get(mod, "m_RemovedComponents", json::array()))
            removed.insert(RefFileId(r));

        for (const json& m : Get(mod, "m_Modifications", json::array())) {
            int64_t target = RefFileId(Get(m, "target", json()));
            std::string path = UnityYaml::String(Get(m, "propertyPath", json("")));
            const json& value = Get(m, "value", json(""));
            const json& objectRef = Get(m, "objectReference", json());

            EntityID entity = 0;
            Slot slot;
            if (auto s = child.slots.find(target); s != child.slots.end()) {
                slot = s->second;
                entity = slot.entity;
            } else if (auto e = child.entities.find(target); e != child.entities.end()) {
                entity = e->second;
            } else if (child.model) {
                if (IsTransformProperty(path)) {
                    if (!modelRootTransform)
                        modelRootTransform = target;
                    if (target == modelRootTransform) {
                        entity = child.root;
                        slot = {child.root, Kind::Transform, -1};
                    }
                } else if (path == "m_Name" || path == "m_IsActive" || path == "m_Layer" || path == "m_TagString") {
                    if (!modelRootObject)
                        modelRootObject = target;
                    if (target == modelRootObject)
                        entity = child.root;
                }
            }
            Entity* e = m_Scene.Get(entity);
            if (!e)
                continue;

            if (slot.kind == Kind::Other || (slot.kind != Kind::Transform && slot.kind != Kind::Script &&
                                             (path == "m_Name" || path == "m_IsActive" || path == "m_Layer" || path == "m_TagString"))) {
                // GameObject properties.
                if (path == "m_Name") e->name = UnityYaml::String(value);
                else if (path == "m_IsActive") e->active = UnityYaml::Integer(value, 1) != 0;
                else if (path == "m_Layer") e->layer = int(UnityYaml::Integer(value));
                else if (path == "m_TagString") e->tag = UnityYaml::String(value);
                continue;
            }
            ApplyComponentProperty(*e, slot, path, value, objectRef, context);
        }

        // Removed components.
        for (int64_t r : removed) {
            auto s = child.slots.find(r);
            if (s == child.slots.end())
                continue;
            Entity* e = m_Scene.Get(s->second.entity);
            if (!e)
                continue;
            switch (s->second.kind) {
            case Kind::MeshFilter: case Kind::MeshRenderer: case Kind::SkinnedMeshRenderer: e->meshRenderer.reset(); break;
            case Kind::BoxCollider: case Kind::SphereCollider: case Kind::CapsuleCollider: case Kind::MeshCollider:
            case Kind::CharacterController: e->collider.reset(); break;
            case Kind::Rigidbody: e->rigidbody.reset(); break;
            case Kind::Light: e->light.reset(); break;
            case Kind::Camera: e->camera.reset(); break;
            case Kind::Script:
                if (s->second.script >= 0 && size_t(s->second.script) < e->scripts.size())
                    e->scripts[size_t(s->second.script)].className.clear(); // removed at the end
                break;
            default: break;
            }
        }
        for (const json& r : Get(mod, "m_RemovedGameObjects", json::array()))
            if (auto e = child.entities.find(RefFileId(r)); e != child.entities.end())
                if (m_Scene.Get(e->second))
                    m_Scene.DestroyEntity(e->second);
    }

    static void SetAxis(glm::vec3& v, char axis, float value)
    {
        if (axis == 'x') v.x = value;
        else if (axis == 'y') v.y = value;
        else if (axis == 'z') v.z = value;
    }

    void ApplyComponentProperty(Entity& e, const Slot& slot, const std::string& path, const json& value,
                                const json& objectRef, int context)
    {
        float f = float(UnityYaml::Number(value));
        char axis = path.empty() ? 0 : path.back();
        auto starts = [&](const char* prefix) { return path.rfind(prefix, 0) == 0; };
        switch (slot.kind) {
        case Kind::Transform:
            if (starts("m_LocalPosition.")) SetAxis(e.transform.position, axis, f);
            else if (starts("m_LocalScale.")) SetAxis(e.transform.scale, axis, f);
            else if (starts("m_LocalRotation.")) {
                glm::quat& q = e.transform.rotation;
                if (axis == 'x') q.x = f;
                else if (axis == 'y') q.y = f;
                else if (axis == 'z') q.z = f;
                else if (axis == 'w') q.w = f;
            }
            break;
        case Kind::BoxCollider: case Kind::SphereCollider: case Kind::CapsuleCollider: case Kind::CharacterController:
            if (!e.collider) break;
            if (starts("m_Size.")) SetAxis(e.collider->size, axis, f);
            else if (starts("m_Center.")) SetAxis(e.collider->center, axis, f);
            else if (path == "m_Radius") e.collider->radius = f;
            else if (path == "m_Height") e.collider->height = f;
            else if (path == "m_IsTrigger") e.collider->isTrigger = f != 0.0f;
            else if (path == "m_Enabled" && f == 0.0f) e.collider.reset();
            break;
        case Kind::Rigidbody:
            if (!e.rigidbody) break;
            if (path == "m_Mass") e.rigidbody->mass = f;
            else if (path == "m_IsKinematic") e.rigidbody->isKinematic = f != 0.0f;
            else if (path == "m_UseGravity") e.rigidbody->useGravity = f != 0.0f;
            else if (path == "m_Drag" || path == "m_LinearDamping") e.rigidbody->linearDamping = f;
            else if (path == "m_AngularDrag" || path == "m_AngularDamping") e.rigidbody->angularDamping = f;
            break;
        case Kind::Light:
            if (!e.light) break;
            if (path == "m_Intensity") e.light->intensity = f;
            else if (path == "m_Color.r") e.light->color.r = f;
            else if (path == "m_Color.g") e.light->color.g = f;
            else if (path == "m_Color.b") e.light->color.b = f;
            else if (path == "m_Enabled" && f == 0.0f) e.light.reset();
            break;
        case Kind::Camera:
            if (!e.camera) break;
            if (path == "field of view") e.camera->fieldOfView = f;
            else if (path == "near clip plane") e.camera->nearClip = f;
            else if (path == "far clip plane") e.camera->farClip = f;
            break;
        case Kind::MeshRenderer: case Kind::SkinnedMeshRenderer:
            if (path == "m_Enabled" && f == 0.0f) {
                e.meshRenderer.reset();
            } else if (starts("m_Materials.Array.data[") && e.meshRenderer && path.find("[0]") != std::string::npos) {
                ApplyMaterial(*e.meshRenderer, objectRef);
            }
            break;
        case Kind::Script: {
            if (slot.script < 0 || size_t(slot.script) >= e.scripts.size())
                break;
            ScriptComponent& s = e.scripts[size_t(slot.script)];
            if (path == "m_Enabled") {
                s.enabled = f != 0.0f;
                break;
            }
            json fields = json::parse(s.fields, nullptr, false);
            if (!fields.is_object())
                fields = json::object();
            bool isRef = RefFileId(objectRef) != 0 || RefGuid(objectRef).size();
            SetFieldPath(fields, path, isRef ? ConvertRef(objectRef, context) : value);
            s.fields = fields.dump();
            break;
        }
        default:
            break;
        }
    }

    // Applies "a.b.Array.data[2].c" / "list.Array.size" / "offset.x" to converted field JSON.
    static void SetFieldPath(json& root, const std::string& path, const json& value)
    {
        std::vector<std::string> parts;
        size_t start = 0;
        while (start <= path.size()) {
            size_t dot = path.find('.', start);
            parts.push_back(path.substr(start, dot == std::string::npos ? std::string::npos : dot - start));
            if (dot == std::string::npos)
                break;
            start = dot + 1;
        }
        json* node = &root;
        for (size_t i = 0; i < parts.size(); ++i) {
            const std::string& p = parts[i];
            bool last = i + 1 == parts.size();
            if (p == "Array")
                continue;
            if (p == "size") {
                if (!node->is_array())
                    *node = json::array();
                size_t n = size_t(std::max<int64_t>(0, UnityYaml::Integer(value)));
                while (node->size() < n)
                    node->push_back(node->empty() ? json(nullptr) : node->back());
                while (node->size() > n)
                    node->erase(node->size() - 1);
                return;
            }
            if (p.rfind("data[", 0) == 0) {
                size_t index = size_t(std::atoll(p.c_str() + 5));
                if (!node->is_array())
                    *node = json::array();
                while (node->size() <= index)
                    node->push_back(nullptr);
                node = &(*node)[index];
                if (last)
                    *node = value;
                continue;
            }
            // Vector / color components are stored as arrays after conversion.
            static const std::string axes = "xyzw", rgba = "rgba";
            if (node->is_array() && p.size() == 1 && (axes.find(p[0]) != std::string::npos || rgba.find(p[0]) != std::string::npos)) {
                size_t index = axes.find(p[0]) != std::string::npos ? axes.find(p[0]) : rgba.find(p[0]);
                while (node->size() <= index)
                    node->push_back(0.0);
                (*node)[index] = UnityYaml::Number(value);
                return;
            }
            if (!node->is_object())
                *node = json::object();
            if (last) {
                (*node)[p] = value;
                return;
            }
            // Vectors inside structs arrive as "pos.x": create the array form directly.
            const std::string& next = parts[i + 1];
            if (!node->contains(p) && next.size() == 1 && (axes.find(next[0]) != std::string::npos || rgba.find(next[0]) != std::string::npos))
                (*node)[p] = json::array();
            node = &(*node)[p];
        }
    }

    UnityAssetDatabase& m_Db;
    Scene& m_Scene;
    const UnityImportOptions& m_Options;
    UnityImportReport& m_Report;
    std::vector<std::unordered_map<int64_t, EntityID>> m_Contexts;
    std::unordered_map<EntityID, PendingRenderer> m_Pending;
    std::vector<EntityID> m_NeedsCollider;
    std::vector<std::pair<EntityID, std::vector<EntityID>>> m_ChildOrder;
};

std::vector<EntityID> CreatedSince(const Scene& scene, size_t before)
{
    std::vector<EntityID> ids;
    const auto& entities = scene.Entities();
    for (size_t i = before; i < entities.size(); ++i)
        ids.push_back(entities[i]->id);
    return ids;
}

} // namespace

namespace UnityImporter {

bool IsUnityScene(const fs::path& path) { return Lower(path.extension().string()) == ".unity"; }
bool IsUnityPrefab(const fs::path& path) { return Lower(path.extension().string()) == ".prefab"; }

bool ImportScene(UnityAssetDatabase& db, const std::string& assetPath, Scene& scene, const UnityImportOptions& options,
                 UnityImportReport* report)
{
    UnityImportReport local;
    UnityImportReport& r = report ? *report : local;
    if (db.Documents(assetPath).empty()) {
        Log::Error("Cannot read Unity scene '{}'", assetPath);
        return false;
    }
    scene.Clear();
    Builder builder(db, scene, options, r);
    builder.BuildFile(assetPath, 0, 0);
    builder.ApplyRenderSettings(assetPath);
    builder.ResolveReferences(CreatedSince(scene, 0));
    builder.AddRootOrder(assetPath);
    builder.ApplyChildOrder();
    for (const Document& d : db.Documents(assetPath))
        if (d.classId == 218 || d.classId == 154)
            ++r.skipped[d.classId == 218 ? "Terrain" : "TerrainCollider"];
    scene.UpdateWorldTransforms();
    return true;
}

EntityID InstantiatePrefab(UnityAssetDatabase& db, const std::string& assetPath, Scene& scene, EntityID parent,
                           const UnityImportOptions& options, UnityImportReport* report)
{
    UnityImportReport local;
    UnityImportReport& r = report ? *report : local;
    if (db.Documents(assetPath).empty()) {
        Log::Error("Cannot read Unity prefab '{}'", assetPath);
        return 0;
    }
    size_t before = scene.Entities().size();
    Builder builder(db, scene, options, r);
    auto inst = builder.BuildFile(assetPath, parent, 0);
    builder.ResolveReferences(CreatedSince(scene, before));
    builder.ApplyChildOrder();
    scene.UpdateWorldTransforms();
    return inst.root;
}

json LoadScriptAsset(UnityAssetDatabase& db, const std::string& assetPath)
{
    const std::vector<Document>& docs = db.Documents(assetPath);
    // The main object of a .asset file is 11400000; other MonoBehaviours are sub-assets.
    const Document* main = nullptr;
    for (const Document& d : docs)
        if (d.classId == 114 && (!main || d.fileId == 11400000))
            main = &d;
    if (!main)
        return nullptr;
    std::string scriptPath = db.PathForGuid(RefGuid(Get(main->body, "m_Script", json())));
    if (scriptPath.empty() || Lower(fs::path(scriptPath).extension().string()) != ".cs")
        return nullptr;
    Scene scratch;
    UnityImportReport report;
    UnityImportOptions options;
    Builder builder(db, scratch, options, report);
    return json{{"class", Platform::Utf8ToPath(scriptPath).stem().string()},
                {"name", UnityYaml::String(Get(main->body, "m_Name", json("")))},
                {"fields", builder.ConvertFields(main->body)}};
}

MaterialInfo LoadMaterial(UnityAssetDatabase& db, const std::string& assetPath)
{
    MaterialInfo info;
    for (const Document& d : db.Documents(assetPath)) {
        if (d.classId != 21)
            continue;
        const json props = Get(d.body, "m_SavedProperties", json::object());
        auto findEntry = [&](const char* list, const char* name) -> const json* {
            auto it = props.find(list);
            if (it == props.end() || !it->is_array())
                return nullptr;
            for (const json& entry : *it)
                if (entry.is_object() && entry.contains(name))
                    return &entry[name];
            return nullptr;
        };
        const json* color = findEntry("m_Colors", "_BaseColor");
        if (!color)
            color = findEntry("m_Colors", "_Color");
        if (color)
            info.color = Color(*color);
        const json* tex = findEntry("m_TexEnvs", "_BaseMap");
        if (!tex || RefGuid(Get(*tex, "m_Texture")).empty())
            tex = findEntry("m_TexEnvs", "_MainTex");
        if (tex) {
            std::string path = db.PathForGuid(RefGuid(Get(*tex, "m_Texture")));
            if (IsImagePath(path))
                info.texture = path;
        }
        break;
    }
    return info;
}

} // namespace UnityImporter
} // namespace ie
