#include "IndeetsEngine/Scripting/ScriptEngine.h"

#include "IndeetsEngine/Core/Input.h"
#include "IndeetsEngine/Core/Log.h"
#include "IndeetsEngine/Core/Platform.h"
#include "IndeetsEngine/Physics/PhysicsWorld.h"
#include "IndeetsEngine/Scene/Primitives.h"
#include "IndeetsEngine/Scripting/DotNetHost.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace ie {
namespace {

using json = nlohmann::json;

// ---------------------------------------------------------------------------------------------
// Native API table handed to the managed side. The member order is the ABI: it must match
// IndeetsEngine.Interop.NativeApi (Engine/ScriptCore/Interop/NativeApi.cs) exactly.
struct ScriptNativeApi {
    void (*Log)(int level, const char* message);
    void (*SetResult)(const char* text);

    int (*EntityExists)(uint32_t id);
    uint32_t (*EntityCreate)(const char* name);
    uint32_t (*EntityCreatePrimitive)(int unityType, const char* name);
    void (*EntityDestroy)(uint32_t id);
    uint32_t (*EntityInstantiate)(uint32_t id, uint32_t parent, int worldPositionStays);
    int (*EntityGetSubtree)(uint32_t root, uint32_t* out, int capacity);
    uint32_t (*EntityFind)(const char* name);
    const char* (*EntityGetName)(uint32_t id);
    void (*EntitySetName)(uint32_t id, const char* name);
    int (*EntityGetActive)(uint32_t id);
    void (*EntitySetActive)(uint32_t id, int active);
    void (*EntityGetActiveStates)(const uint32_t* ids, int count, uint8_t* out);
    uint32_t (*EntityGetParent)(uint32_t id);
    void (*EntitySetParent)(uint32_t id, uint32_t parent, int worldPositionStays);
    int (*EntityGetChildren)(uint32_t id, uint32_t* out, int capacity);
    int (*EntityGetAll)(uint32_t* out, int capacity);

    void (*TransformGetLocal)(uint32_t id, float* position, float* rotation, float* scale);
    void (*TransformSetLocal)(uint32_t id, const float* position, const float* rotation, const float* scale);
    void (*TransformGetWorld)(uint32_t id, float* position, float* rotation, float* lossyScale);
    void (*TransformSetWorld)(uint32_t id, const float* position, const float* rotation);

    int (*ComponentHas)(uint32_t id, int kind);
    void (*ComponentAdd)(uint32_t id, int kind);
    void (*ComponentRemove)(uint32_t id, int kind);
    int (*ComponentGet)(uint32_t id, int kind, int prop, float* out);
    void (*ComponentSet)(uint32_t id, int kind, int prop, const float* values);
    const char* (*ComponentGetString)(uint32_t id, int kind, int prop);
    void (*ComponentSetString)(uint32_t id, int kind, int prop, const char* value);

    void (*RigidbodyAddForce)(uint32_t id, const float* force, int mode);
    int (*PhysicsRaycast)(const float* origin, const float* direction, float maxDistance, float* hit, uint32_t* entity);

    int (*InputKey)(int key, int query);
    int (*InputMouseButton)(int button, int query);
    void (*InputMouseState)(float* out);
    void (*InputSetCursorLocked)(int locked);
    void (*ScreenSize)(float* out);
    uint32_t (*MainCamera)();

    const char* (*EntityGetTag)(uint32_t id);
    void (*EntitySetTag)(uint32_t id, const char* tag);
    int (*EntityGetLayer)(uint32_t id);
    void (*EntitySetLayer)(uint32_t id, int layer);
    const char* (*EntityGetScripts)(uint32_t id);
    const char* (*AssetFindResources)(const char* path, int all);
    const char* (*AssetDescribe)(const char* assetPath);
    uint32_t (*AssetPrefabTemplate)(const char* assetPath);
};

// Built-in component kinds (IndeetsEngine.Interop.BuiltinKind).
enum ScriptBuiltin { kMeshRenderer = 1, kCollider = 2, kRigidbody = 3, kLight = 4, kCamera = 5 };

// ---------------------------------------------------------------------------------------------
// Managed entry points (IndeetsEngine.Interop.Bridge).
struct ManagedApi {
    int (*Initialize)(const ScriptNativeApi* api, int isEditor) = nullptr;
    void (*SetApplicationInfo)(const char* json) = nullptr;
    int (*CompileAsync)(const char* request) = nullptr;
    int (*PollCompile)() = nullptr;
    int (*LoadAssembly)(const char* path) = nullptr;
    int (*DescribeScripts)() = nullptr;
    int (*BeginPlay)(const char* scene) = nullptr;
    void (*FixedUpdate)(float fixedDeltaTime) = nullptr;
    void (*DispatchContacts)(const ContactEvent* events, int count) = nullptr;
    void (*Update)(float unscaledDeltaTime) = nullptr;
    void (*LateUpdate)() = nullptr;
    void (*EndPlay)() = nullptr;
    float (*GetTimeScale)() = nullptr;
    int (*ConsumeQuitRequest)() = nullptr;
    void (*DestroyEntity)(uint32_t id) = nullptr;
    int (*GetEntityScripts)(uint32_t id) = nullptr;
    int (*SetScriptField)(uint32_t id, int index, const char* name, const char* value) = nullptr;
    int (*SetScriptEnabled)(uint32_t id, int index, int enabled) = nullptr;
    int (*AddScript)(uint32_t id, const char* className, const char* fields) = nullptr;
    int (*RemoveScript)(uint32_t id, int index) = nullptr;
};

DotNetHost g_Host;
ManagedApi g_Managed;
ScriptNativeApi g_Api{};

// The scene scripts run against (only while playing, or while a player runs the game).
Scene* g_Scene = nullptr;
PhysicsWorld* g_Physics = nullptr;
std::string g_Result;
thread_local std::string t_String;

ScriptAssetProvider g_Assets;
// Prefab assets scripts reference, materialized once per play session below a hidden, inactive
// container (like Unity's prefab assets: never awake, found by nothing, cloned by Instantiate).
EntityID g_TemplateRoot = 0;
std::unordered_map<std::string, EntityID> g_Templates;

glm::vec2 g_ViewportOrigin{0.0f};
glm::vec2 g_ViewportSize{1280.0f, 720.0f};
bool g_InputEnabled = true;

Entity* E(uint32_t id) { return g_Scene ? g_Scene->Get(id) : nullptr; }

// Entities below the prefab asset container are assets, not scene objects.
bool IsAssetEntity(const Entity& e)
{
    if (!g_TemplateRoot)
        return false;
    const Entity* current = &e;
    for (int guard = 0; current && guard < 1024; ++guard) {
        if (current->id == g_TemplateRoot)
            return true;
        current = current->parent ? E(current->parent) : nullptr;
    }
    return false;
}

// World matrix computed from the local transforms up the chain (never stale mid-frame).
glm::mat4 WorldMatrix(const Entity& e)
{
    glm::mat4 m = e.transform.Matrix();
    EntityID parent = e.parent;
    for (int guard = 0; parent != kInvalidEntity && guard < 1024; ++guard) {
        const Entity* p = E(parent);
        if (!p)
            break;
        m = p->transform.Matrix() * m;
        parent = p->parent;
    }
    return m;
}

glm::mat4 ParentWorldMatrix(const Entity& e)
{
    const Entity* p = e.parent ? E(e.parent) : nullptr;
    return p ? WorldMatrix(*p) : glm::mat4(1.0f);
}

// Re-creates the physics body after a component or activity change, keeping its velocity.
void RebuildBody(Entity& e)
{
    if (!g_Physics || !g_Physics->IsRunning())
        return;
    glm::vec3 velocity = g_Physics->GetLinearVelocity(e.id);
    glm::vec3 angular = g_Physics->GetAngularVelocity(e.id);
    bool had = g_Physics->HasBody(e.id);
    g_Physics->RemoveEntity(e.id);
    if (!e.collider || !g_Scene->IsActiveInHierarchy(e.id))
        return;
    e.world = WorldMatrix(e);
    g_Physics->AddEntity(e);
    if (had) {
        g_Physics->SetLinearVelocity(e.id, velocity);
        g_Physics->SetAngularVelocity(e.id, angular);
    }
}

void RebuildSubtree(EntityID root)
{
    if (!g_Scene)
        return;
    for (EntityID id : g_Scene->Subtree(root))
        if (Entity* e = E(id))
            RebuildBody(*e);
}

// A script moved an entity: move its body and its children's bodies along.
void SyncBodies(EntityID root)
{
    if (!g_Physics || !g_Physics->IsRunning() || !g_Scene)
        return;
    for (EntityID id : g_Scene->Subtree(root)) {
        Entity* e = E(id);
        if (!e || !g_Physics->HasBody(id))
            continue;
        e->world = WorldMatrix(*e);
        glm::vec3 p, s;
        glm::quat r;
        DecomposeWorld(e->world, p, r, s);
        g_Physics->Teleport(id, p, r);
    }
}

int CopyIds(const std::vector<EntityID>& ids, uint32_t* out, int capacity)
{
    int n = static_cast<int>(ids.size());
    if (out)
        for (int i = 0; i < n && i < capacity; ++i)
            out[i] = ids[static_cast<size_t>(i)];
    return n;
}

PrimitiveType FromUnityPrimitive(int type)
{
    switch (type) {
    case 0: return PrimitiveType::Sphere;
    case 1: return PrimitiveType::Capsule;
    case 2: return PrimitiveType::Cylinder;
    case 4: return PrimitiveType::Plane;
    case 5: return PrimitiveType::Quad;
    default: return PrimitiveType::Cube;
    }
}

// ---------------------------------------------------------------------------------------------
// Native API implementation

void ApiLog(int level, const char* message)
{
    std::string text = message ? message : "";
    switch (level) {
    case 1: Log::Write(LogLevel::Warning, std::move(text)); break;
    case 2: Log::Write(LogLevel::Error, std::move(text)); break;
    default: Log::Write(LogLevel::Info, std::move(text)); break;
    }
}

void ApiSetResult(const char* text) { g_Result = text ? text : ""; }

int ApiEntityExists(uint32_t id) { return E(id) ? 1 : 0; }

uint32_t ApiEntityCreate(const char* name)
{
    if (!g_Scene)
        return 0;
    Entity& e = g_Scene->CreateEntity(name && *name ? name : "New Game Object");
    e.world = e.transform.Matrix();
    return e.id;
}

uint32_t ApiEntityCreatePrimitive(int unityType, const char* name)
{
    if (!g_Scene)
        return 0;
    Entity& e = g_Scene->CreatePrimitive(FromUnityPrimitive(unityType), name ? name : "");
    e.world = e.transform.Matrix();
    RebuildBody(e);
    return e.id;
}

void ApiEntityDestroy(uint32_t id)
{
    if (!E(id))
        return;
    if (g_Physics)
        for (EntityID child : g_Scene->Subtree(id))
            g_Physics->RemoveEntity(child);
    g_Scene->DestroyEntity(id);
}

uint32_t ApiEntityInstantiate(uint32_t id, uint32_t parent, int worldPositionStays)
{
    if (!E(id))
        return 0;
    EntityID copy = g_Scene->Duplicate(id).id;
    if (parent && !E(parent))
        parent = 0;
    Entity* c = E(copy);
    if (c && c->parent != parent) {
        // No parent: the clone goes to the scene root where the original was. With a parent,
        // worldPositionStays decides between keeping the world pose and reusing the local one.
        bool keepWorld = parent == 0 || worldPositionStays != 0;
        if (!keepWorld) {
            c->parent = parent;
        } else {
            glm::mat4 world = WorldMatrix(*c);
            c->parent = parent;
            glm::mat4 local = glm::inverse(ParentWorldMatrix(*c)) * world;
            glm::vec3 p, s;
            glm::quat r;
            DecomposeWorld(local, p, r, s);
            c->transform.position = p;
            c->transform.rotation = r;
            c->transform.scale = s;
        }
    }
    RebuildSubtree(copy);
    return copy;
}

int ApiEntityGetSubtree(uint32_t root, uint32_t* out, int capacity)
{
    return g_Scene ? CopyIds(g_Scene->Subtree(root), out, capacity) : 0;
}

uint32_t ApiEntityFind(const char* name)
{
    if (!g_Scene || !name)
        return 0;
    // GameObject.Find also accepts "Parent/Child" paths.
    std::string path = name;
    if (path.find('/') == std::string::npos) {
        for (const auto& e : g_Scene->Entities())
            if (e->name == path && !IsAssetEntity(*e))
                return e->id;
        return 0;
    }
    std::vector<std::string> parts;
    size_t start = path[0] == '/' ? 1 : 0;
    while (start <= path.size()) {
        size_t end = path.find('/', start);
        parts.push_back(path.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    for (const auto& root : g_Scene->Entities()) {
        if (root->parent != kInvalidEntity || root->name != parts[0] || root->id == g_TemplateRoot)
            continue;
        EntityID current = root->id;
        for (size_t i = 1; i < parts.size() && current; ++i) {
            EntityID next = 0;
            for (EntityID child : g_Scene->Children(current))
                if (const Entity* ce = E(child); ce && ce->name == parts[i]) {
                    next = child;
                    break;
                }
            current = next;
        }
        if (current)
            return current;
    }
    return 0;
}

const char* ApiEntityGetName(uint32_t id)
{
    const Entity* e = E(id);
    t_String = e ? e->name : "";
    return t_String.c_str();
}

void ApiEntitySetName(uint32_t id, const char* name)
{
    if (Entity* e = E(id))
        e->name = name ? name : "";
}

int ApiEntityGetActive(uint32_t id)
{
    const Entity* e = E(id);
    return e && e->active ? 1 : 0;
}

void ApiEntitySetActive(uint32_t id, int active)
{
    Entity* e = E(id);
    if (!e || e->active == (active != 0))
        return;
    e->active = active != 0;
    RebuildSubtree(id);
}

void ApiEntityGetActiveStates(const uint32_t* ids, int count, uint8_t* out)
{
    for (int i = 0; i < count; ++i)
        out[i] = g_Scene && g_Scene->IsActiveInHierarchy(ids[i]) ? 1 : 0;
}

uint32_t ApiEntityGetParent(uint32_t id)
{
    const Entity* e = E(id);
    if (!e)
        return 0;
    if (e->parent)
        return e->parent == g_TemplateRoot ? 0 : e->parent; // prefab assets have no parent
    return e->rectTransform ? e->rectTransform->parent : 0;
}

void ApiEntitySetParent(uint32_t id, uint32_t parent, int worldPositionStays)
{
    Entity* e = E(id);
    if (!e || id == parent || (parent && (!E(parent) || g_Scene->IsDescendant(parent, id))))
        return;
    glm::mat4 world = WorldMatrix(*e);
    e->parent = parent;
    if (worldPositionStays) {
        glm::mat4 local = glm::inverse(ParentWorldMatrix(*e)) * world;
        glm::vec3 p, s;
        glm::quat r;
        DecomposeWorld(local, p, r, s);
        e->transform.position = p;
        e->transform.rotation = r;
        e->transform.scale = s;
    }
    RebuildSubtree(id); // activity may have changed with the new parent
}

int ApiEntityGetChildren(uint32_t id, uint32_t* out, int capacity)
{
    return g_Scene ? CopyIds(g_Scene->Children(id), out, capacity) : 0;
}

int ApiEntityGetAll(uint32_t* out, int capacity)
{
    if (!g_Scene)
        return 0;
    int n = 0;
    for (const auto& e : g_Scene->Entities()) {
        if (IsAssetEntity(*e))
            continue;
        if (out && n < capacity)
            out[n] = e->id;
        ++n;
    }
    return n;
}

void WriteVec3(float* out, const glm::vec3& v)
{
    out[0] = v.x;
    out[1] = v.y;
    out[2] = v.z;
}

void WriteQuat(float* out, const glm::quat& q)
{
    out[0] = q.x;
    out[1] = q.y;
    out[2] = q.z;
    out[3] = q.w;
}

glm::quat ReadQuat(const float* in) { return glm::normalize(glm::quat(in[3], in[0], in[1], in[2])); }

void ApiTransformGetLocal(uint32_t id, float* position, float* rotation, float* scale)
{
    const Entity* e = E(id);
    Transform t = e ? e->transform : Transform{};
    WriteVec3(position, t.position);
    WriteQuat(rotation, t.rotation);
    WriteVec3(scale, t.scale);
}

void ApiTransformSetLocal(uint32_t id, const float* position, const float* rotation, const float* scale)
{
    Entity* e = E(id);
    if (!e)
        return;
    if (position)
        e->transform.position = {position[0], position[1], position[2]};
    if (rotation)
        e->transform.rotation = ReadQuat(rotation);
    if (scale)
        e->transform.scale = {scale[0], scale[1], scale[2]};
    if (scale && g_Physics && g_Physics->HasBody(id))
        RebuildSubtree(id); // collider shapes are baked with the scale
    else
        SyncBodies(id);
}

void ApiTransformGetWorld(uint32_t id, float* position, float* rotation, float* lossyScale)
{
    const Entity* e = E(id);
    glm::vec3 p(0.0f), s(1.0f);
    glm::quat r(1.0f, 0.0f, 0.0f, 0.0f);
    if (e)
        DecomposeWorld(WorldMatrix(*e), p, r, s);
    WriteVec3(position, p);
    WriteQuat(rotation, r);
    WriteVec3(lossyScale, s);
}

void ApiTransformSetWorld(uint32_t id, const float* position, const float* rotation)
{
    Entity* e = E(id);
    if (!e)
        return;
    glm::mat4 parentWorld = ParentWorldMatrix(*e);
    glm::vec3 wp, ws;
    glm::quat wr;
    DecomposeWorld(parentWorld * e->transform.Matrix(), wp, wr, ws);
    if (position)
        wp = {position[0], position[1], position[2]};
    if (rotation)
        wr = ReadQuat(rotation);
    glm::mat4 world = glm::translate(glm::mat4(1.0f), wp) * glm::mat4_cast(wr) * glm::scale(glm::mat4(1.0f), ws);
    glm::mat4 local = glm::inverse(parentWorld) * world;
    glm::vec3 lp, ls;
    glm::quat lr;
    DecomposeWorld(local, lp, lr, ls);
    // Only what was asked for changes; keeping the local scale avoids decomposition drift.
    if (position)
        e->transform.position = lp;
    if (rotation)
        e->transform.rotation = glm::normalize(lr);
    SyncBodies(id);
}

bool HasKind(const Entity& e, int kind)
{
    switch (kind) {
    case kMeshRenderer: return e.meshRenderer.has_value();
    case kCollider: return e.collider.has_value();
    case kRigidbody: return e.rigidbody.has_value();
    case kLight: return e.light.has_value();
    case kCamera: return e.camera.has_value();
    default: return false;
    }
}

int ApiComponentHas(uint32_t id, int kind)
{
    const Entity* e = E(id);
    return e && HasKind(*e, kind) ? 1 : 0;
}

void ApiComponentAdd(uint32_t id, int kind)
{
    Entity* e = E(id);
    if (!e || HasKind(*e, kind))
        return;
    switch (kind) {
    case kMeshRenderer: e->meshRenderer = MeshRendererComponent{}; break;
    case kCollider: e->collider = ColliderComponent{}; break;
    case kRigidbody: e->rigidbody = RigidbodyComponent{}; break;
    case kLight: e->light = LightComponent{}; break;
    case kCamera: e->camera = CameraComponent{}; break;
    default: return;
    }
    if (kind == kCollider || kind == kRigidbody)
        RebuildBody(*e);
}

void ApiComponentRemove(uint32_t id, int kind)
{
    Entity* e = E(id);
    if (!e)
        return;
    switch (kind) {
    case kMeshRenderer: e->meshRenderer.reset(); break;
    case kCollider: e->collider.reset(); break;
    case kRigidbody: e->rigidbody.reset(); break;
    case kLight: e->light.reset(); break;
    case kCamera: e->camera.reset(); break;
    default: return;
    }
    if (kind == kCollider || kind == kRigidbody)
        RebuildBody(*e);
}

int ApiComponentGet(uint32_t id, int kind, int prop, float* out)
{
    const Entity* e = E(id);
    if (!e || !HasKind(*e, kind))
        return 0;
    auto vec3 = [&](const glm::vec3& v) { WriteVec3(out, v); };
    switch (kind) {
    case kMeshRenderer:
        if (prop == 0) {
            const glm::vec4& c = e->meshRenderer->color;
            out[0] = c.r; out[1] = c.g; out[2] = c.b; out[3] = c.a;
        }
        break;
    case kCollider: {
        const ColliderComponent& c = *e->collider;
        switch (prop) {
        case 0: out[0] = static_cast<float>(c.shape); break;
        case 1: vec3(c.center); break;
        case 2: vec3(c.size); break;
        case 3: out[0] = c.radius; break;
        case 4: out[0] = c.height; break;
        case 5: out[0] = c.isTrigger ? 1.0f : 0.0f; break;
        case 6: out[0] = c.friction; break;
        case 7: out[0] = c.bounciness; break;
        default: return 0;
        }
        break;
    }
    case kRigidbody: {
        const RigidbodyComponent& rb = *e->rigidbody;
        switch (prop) {
        case 0: out[0] = rb.mass; break;
        case 1: out[0] = rb.linearDamping; break;
        case 2: out[0] = rb.angularDamping; break;
        case 3: out[0] = rb.useGravity ? 1.0f : 0.0f; break;
        case 4: out[0] = rb.isKinematic ? 1.0f : 0.0f; break;
        case 5: vec3(g_Physics ? g_Physics->GetLinearVelocity(id) : glm::vec3(0.0f)); break;
        case 6: vec3(g_Physics ? g_Physics->GetAngularVelocity(id) : glm::vec3(0.0f)); break;
        default: return 0;
        }
        break;
    }
    case kLight:
        if (prop == 0)
            vec3(e->light->color);
        else if (prop == 1)
            out[0] = e->light->intensity;
        break;
    case kCamera:
        if (prop == 0) out[0] = e->camera->fieldOfView;
        else if (prop == 1) out[0] = e->camera->nearClip;
        else if (prop == 2) out[0] = e->camera->farClip;
        break;
    default: return 0;
    }
    return 1;
}

void ApiComponentSet(uint32_t id, int kind, int prop, const float* v)
{
    Entity* e = E(id);
    if (!e || !HasKind(*e, kind))
        return;
    glm::vec3 v3(v[0], v[1], v[2]);
    switch (kind) {
    case kMeshRenderer:
        if (prop == 0)
            e->meshRenderer->color = {v[0], v[1], v[2], v[3]};
        return;
    case kCollider: {
        ColliderComponent& c = *e->collider;
        switch (prop) {
        case 0: c.shape = static_cast<ColliderShape>(std::clamp(static_cast<int>(v[0]), 0, 2)); break;
        case 1: c.center = v3; break;
        case 2: c.size = v3; break;
        case 3: c.radius = v[0]; break;
        case 4: c.height = v[0]; break;
        case 5: c.isTrigger = v[0] != 0.0f; break;
        case 6: c.friction = v[0]; break;
        case 7: c.bounciness = v[0]; break;
        default: return;
        }
        RebuildBody(*e);
        return;
    }
    case kRigidbody: {
        RigidbodyComponent& rb = *e->rigidbody;
        switch (prop) {
        case 0: rb.mass = std::max(v[0], 0.0001f); break;
        case 1: rb.linearDamping = v[0]; break;
        case 2: rb.angularDamping = v[0]; break;
        case 3: rb.useGravity = v[0] != 0.0f; break;
        case 4: rb.isKinematic = v[0] != 0.0f; break;
        case 5: if (g_Physics) g_Physics->SetLinearVelocity(id, v3); return;
        case 6: if (g_Physics) g_Physics->SetAngularVelocity(id, v3); return;
        default: return;
        }
        RebuildBody(*e);
        return;
    }
    case kLight:
        if (prop == 0) e->light->color = v3;
        else if (prop == 1) e->light->intensity = v[0];
        return;
    case kCamera:
        if (prop == 0) e->camera->fieldOfView = std::clamp(v[0], 1.0f, 179.0f);
        else if (prop == 1) e->camera->nearClip = std::max(v[0], 0.001f);
        else if (prop == 2) e->camera->farClip = v[0];
        return;
    default: return;
    }
}

const char* ApiComponentGetString(uint32_t id, int kind, int prop)
{
    const Entity* e = E(id);
    t_String.clear();
    if (e && kind == kMeshRenderer && e->meshRenderer)
        t_String = prop == 1 ? e->meshRenderer->mesh : prop == 2 ? e->meshRenderer->texture : "";
    return t_String.c_str();
}

void ApiComponentSetString(uint32_t id, int kind, int prop, const char* value)
{
    Entity* e = E(id);
    if (!e || kind != kMeshRenderer || !e->meshRenderer)
        return;
    if (prop == 1)
        e->meshRenderer->mesh = value ? value : "";
    else if (prop == 2)
        e->meshRenderer->texture = value ? value : "";
}

void ApiRigidbodyAddForce(uint32_t id, const float* force, int mode)
{
    Entity* e = E(id);
    if (!e || !e->rigidbody || !g_Physics)
        return;
    glm::vec3 f(force[0], force[1], force[2]);
    float mass = std::max(e->rigidbody->mass, 0.0001f);
    switch (mode) {
    case 1: g_Physics->AddImpulse(id, f); break;                 // Impulse
    case 2: g_Physics->AddImpulse(id, f * mass); break;          // VelocityChange
    case 5: g_Physics->AddForce(id, f * mass); break;            // Acceleration
    default: g_Physics->AddForce(id, f); break;                  // Force
    }
}

int ApiPhysicsRaycast(const float* origin, const float* direction, float maxDistance, float* hit, uint32_t* entity)
{
    if (!g_Physics)
        return 0;
    auto result = g_Physics->Raycast({origin[0], origin[1], origin[2]}, {direction[0], direction[1], direction[2]},
                                     maxDistance);
    if (!result)
        return 0;
    WriteVec3(hit, result->point);
    WriteVec3(hit + 3, result->normal);
    hit[6] = result->distance;
    *entity = result->entity;
    return 1;
}

int ApiInputKey(int key, int query)
{
    if (!g_InputEnabled)
        return 0;
    Key k = static_cast<Key>(key);
    switch (query) {
    case 1: return Input::GetKeyDown(k) ? 1 : 0;
    case 2: return Input::GetKeyUp(k) ? 1 : 0;
    default: return Input::GetKey(k) ? 1 : 0;
    }
}

int ApiInputMouseButton(int button, int query)
{
    if (!g_InputEnabled || button < 0 || button > 7)
        return 0;
    MouseButton b = static_cast<MouseButton>(button);
    switch (query) {
    case 1: return Input::GetMouseButtonDown(b) ? 1 : 0;
    case 2: return Input::GetMouseButtonUp(b) ? 1 : 0;
    default: return Input::GetMouseButton(b) ? 1 : 0;
    }
}

void ApiInputMouseState(float* out)
{
    // Unity: origin at the bottom-left of the game view, +Y up.
    glm::vec2 mouse = Input::MousePosition() - g_ViewportOrigin;
    glm::vec2 delta = Input::MouseDelta();
    out[0] = mouse.x;
    out[1] = g_ViewportSize.y - mouse.y;
    out[2] = g_InputEnabled ? delta.x : 0.0f;
    out[3] = g_InputEnabled ? -delta.y : 0.0f;
    out[4] = g_InputEnabled ? Input::ScrollDelta() : 0.0f;
}

void ApiInputSetCursorLocked(int locked) { Input::SetCursorLocked(locked != 0 && g_InputEnabled); }

void ApiScreenSize(float* out)
{
    out[0] = g_ViewportSize.x;
    out[1] = g_ViewportSize.y;
}

uint32_t ApiMainCamera()
{
    const Entity* camera = g_Scene ? g_Scene->MainCamera() : nullptr;
    return camera ? camera->id : 0;
}

const char* ApiEntityGetTag(uint32_t id)
{
    const Entity* e = E(id);
    t_String = e ? e->tag : "Untagged";
    return t_String.c_str();
}

void ApiEntitySetTag(uint32_t id, const char* tag)
{
    if (Entity* e = E(id))
        e->tag = tag && *tag ? tag : "Untagged";
}

int ApiEntityGetLayer(uint32_t id)
{
    const Entity* e = E(id);
    return e ? e->layer : 0;
}

void ApiEntitySetLayer(uint32_t id, int layer)
{
    if (Entity* e = E(id))
        e->layer = std::clamp(layer, 0, 31);
}

// Scripts stored on an entity (not the live instances): [{class, enabled, fields}].
const char* ApiEntityGetScripts(uint32_t id)
{
    json scripts = json::array();
    if (const Entity* e = E(id))
        for (const ScriptComponent& s : e->scripts) {
            json fields = json::parse(s.fields, nullptr, false);
            scripts.push_back({{"class", s.className}, {"enabled", s.enabled},
                               {"fields", fields.is_object() ? fields : json::object()}});
        }
    t_String = scripts.dump();
    return t_String.c_str();
}

const char* ApiAssetFindResources(const char* path, int all)
{
    json result = json::array();
    if (g_Assets.findResources)
        for (const std::string& asset : g_Assets.findResources(path ? path : "", all != 0))
            result.push_back(asset);
    t_String = result.dump();
    return t_String.c_str();
}

const char* ApiAssetDescribe(const char* assetPath)
{
    json info;
    if (g_Assets.describe && assetPath)
        info = g_Assets.describe(assetPath);
    t_String = info.is_object() ? info.dump() : "{\"kind\":\"missing\"}";
    return t_String.c_str();
}

uint32_t ApiAssetPrefabTemplate(const char* assetPath)
{
    if (!g_Scene || !assetPath || !g_Assets.instantiate)
        return 0;
    std::string path = assetPath;
    if (auto it = g_Templates.find(path); it != g_Templates.end())
        return E(it->second) ? it->second : 0;
    if (!E(g_TemplateRoot)) {
        Entity& container = g_Scene->CreateEntity("[Prefab Assets]");
        container.active = false;
        container.editorHidden = true;
        g_TemplateRoot = container.id;
    }
    EntityID root = g_Assets.instantiate(*g_Scene, path, g_TemplateRoot);
    if (root) {
        if (Entity* e = E(root))
            e->prefab.clear();
        g_Scene->UpdateWorldTransforms();
    }
    g_Templates[path] = root;
    return root;
}

void FillApi(ScriptNativeApi& api)
{
    api.Log = ApiLog;
    api.SetResult = ApiSetResult;
    api.EntityExists = ApiEntityExists;
    api.EntityCreate = ApiEntityCreate;
    api.EntityCreatePrimitive = ApiEntityCreatePrimitive;
    api.EntityDestroy = ApiEntityDestroy;
    api.EntityInstantiate = ApiEntityInstantiate;
    api.EntityGetSubtree = ApiEntityGetSubtree;
    api.EntityFind = ApiEntityFind;
    api.EntityGetName = ApiEntityGetName;
    api.EntitySetName = ApiEntitySetName;
    api.EntityGetActive = ApiEntityGetActive;
    api.EntitySetActive = ApiEntitySetActive;
    api.EntityGetActiveStates = ApiEntityGetActiveStates;
    api.EntityGetParent = ApiEntityGetParent;
    api.EntitySetParent = ApiEntitySetParent;
    api.EntityGetChildren = ApiEntityGetChildren;
    api.EntityGetAll = ApiEntityGetAll;
    api.TransformGetLocal = ApiTransformGetLocal;
    api.TransformSetLocal = ApiTransformSetLocal;
    api.TransformGetWorld = ApiTransformGetWorld;
    api.TransformSetWorld = ApiTransformSetWorld;
    api.ComponentHas = ApiComponentHas;
    api.ComponentAdd = ApiComponentAdd;
    api.ComponentRemove = ApiComponentRemove;
    api.ComponentGet = ApiComponentGet;
    api.ComponentSet = ApiComponentSet;
    api.ComponentGetString = ApiComponentGetString;
    api.ComponentSetString = ApiComponentSetString;
    api.RigidbodyAddForce = ApiRigidbodyAddForce;
    api.PhysicsRaycast = ApiPhysicsRaycast;
    api.InputKey = ApiInputKey;
    api.InputMouseButton = ApiInputMouseButton;
    api.InputMouseState = ApiInputMouseState;
    api.InputSetCursorLocked = ApiInputSetCursorLocked;
    api.ScreenSize = ApiScreenSize;
    api.MainCamera = ApiMainCamera;
    api.EntityGetTag = ApiEntityGetTag;
    api.EntitySetTag = ApiEntitySetTag;
    api.EntityGetLayer = ApiEntityGetLayer;
    api.EntitySetLayer = ApiEntitySetLayer;
    api.EntityGetScripts = ApiEntityGetScripts;
    api.AssetFindResources = ApiAssetFindResources;
    api.AssetDescribe = ApiAssetDescribe;
    api.AssetPrefabTemplate = ApiAssetPrefabTemplate;
}

template <class Fn>
bool Resolve(const std::filesystem::path& assembly, const char* method, Fn& target)
{
    target = reinterpret_cast<Fn>(g_Host.GetFunction(assembly, "IndeetsEngine.Interop.Bridge, IndeetsEngine.ScriptCore", method));
    return target != nullptr;
}

} // namespace

// =============================================================================================

ScriptEngine& ScriptEngine::Get()
{
    static ScriptEngine instance;
    return instance;
}

bool ScriptEngine::Initialize(bool isEditor, const std::filesystem::path& managedDir)
{
    if (m_Initialized)
        return m_Available;
    m_Initialized = true;
    m_ManagedDir = managedDir.empty() ? Platform::ExecutableDir() / "Managed" : managedDir;
    std::filesystem::path assembly = m_ManagedDir / "IndeetsEngine.ScriptCore.dll";
    std::filesystem::path config = m_ManagedDir / "IndeetsEngine.ScriptCore.runtimeconfig.json";

    if (!g_Host.Initialize(config)) {
        m_Error = g_Host.Error();
        Log::Warn("C# scripting is disabled: {}", m_Error);
        return false;
    }

    bool ok = Resolve(assembly, "Initialize", g_Managed.Initialize) &&
              Resolve(assembly, "SetApplicationInfo", g_Managed.SetApplicationInfo) &&
              Resolve(assembly, "CompileAsync", g_Managed.CompileAsync) &&
              Resolve(assembly, "PollCompile", g_Managed.PollCompile) &&
              Resolve(assembly, "LoadAssembly", g_Managed.LoadAssembly) &&
              Resolve(assembly, "DescribeScripts", g_Managed.DescribeScripts) &&
              Resolve(assembly, "BeginPlay", g_Managed.BeginPlay) &&
              Resolve(assembly, "FixedUpdate", g_Managed.FixedUpdate) &&
              Resolve(assembly, "DispatchContacts", g_Managed.DispatchContacts) &&
              Resolve(assembly, "Update", g_Managed.Update) &&
              Resolve(assembly, "LateUpdate", g_Managed.LateUpdate) &&
              Resolve(assembly, "EndPlay", g_Managed.EndPlay) &&
              Resolve(assembly, "GetTimeScale", g_Managed.GetTimeScale) &&
              Resolve(assembly, "ConsumeQuitRequest", g_Managed.ConsumeQuitRequest) &&
              Resolve(assembly, "DestroyEntity", g_Managed.DestroyEntity) &&
              Resolve(assembly, "GetEntityScripts", g_Managed.GetEntityScripts) &&
              Resolve(assembly, "SetScriptField", g_Managed.SetScriptField) &&
              Resolve(assembly, "SetScriptEnabled", g_Managed.SetScriptEnabled) &&
              Resolve(assembly, "AddScript", g_Managed.AddScript) &&
              Resolve(assembly, "RemoveScript", g_Managed.RemoveScript);
    if (!ok) {
        m_Error = g_Host.Error();
        Log::Error("C# scripting is disabled: {}", m_Error);
        return false;
    }

    FillApi(g_Api);
    if (!g_Managed.Initialize(&g_Api, isEditor ? 1 : 0)) {
        m_Error = "The managed engine failed to initialize";
        Log::Error("C# scripting is disabled: {}", m_Error);
        return false;
    }
    m_Available = true;
    Log::Info("C# scripting ready (.NET at {})", Platform::PathToUtf8(g_Host.DotNetRoot()));
    return true;
}

void ScriptEngine::SetApplicationInfo(const std::filesystem::path& dataPath, const std::string& productName,
                                      const std::string& companyName)
{
    if (!m_Available)
        return;
    json info = {{"dataPath", Platform::PathToUtf8(dataPath)}, {"productName", productName}, {"companyName", companyName}};
    g_Managed.SetApplicationInfo(info.dump().c_str());
}

void ScriptEngine::SetAssetProvider(ScriptAssetProvider provider) { g_Assets = std::move(provider); }

std::string ScriptEngine::TakeResult()
{
    std::string result = std::move(g_Result);
    g_Result.clear();
    return result;
}

std::vector<std::filesystem::path> ScriptEngine::FindScriptFiles(const std::filesystem::path& assetsDir)
{
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    if (!std::filesystem::is_directory(assetsDir, ec))
        return files;
    for (auto it = std::filesystem::recursive_directory_iterator(
             assetsDir, std::filesystem::directory_options::skip_permission_denied, ec);
         it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (ec)
            break;
        if (it->is_directory(ec) && it->path().filename() == "Editor") {
            it.disable_recursion_pending(); // editor-only scripts, as in Unity
            continue;
        }
        if (it->is_regular_file(ec) && it->path().extension() == ".cs")
            files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());
    return files;
}

bool ScriptEngine::CompileAsync(const std::vector<std::filesystem::path>& sources)
{
    if (!m_Available || m_Compiling)
        return false;
    json request;
    request["assemblyName"] = "Assembly-CSharp";
    json& files = request["files"] = json::array();
    for (const auto& path : sources)
        files.push_back(Platform::PathToUtf8(std::filesystem::absolute(path)));
    if (!g_Managed.CompileAsync(request.dump().c_str()))
        return false;
    m_Compiling = true;
    return true;
}

ScriptEngine::CompileState ScriptEngine::Poll()
{
    if (!m_Available || !m_Compiling)
        return CompileState::Idle;
    int state = g_Managed.PollCompile();
    if (state == 1)
        return CompileState::Compiling;
    m_Compiling = false;
    if (state == 0)
        return CompileState::Idle;

    json result = json::parse(TakeResult(), nullptr, false);
    m_Diagnostics.clear();
    int errors = 0, warnings = 0;
    if (result.is_object()) {
        for (const json& d : result.value("diagnostics", json::array())) {
            ScriptDiagnostic diag;
            diag.error = d.value("severity", std::string("error")) == "error";
            diag.file = d.value("file", std::string());
            diag.line = d.value("line", 0);
            diag.column = d.value("column", 0);
            diag.id = d.value("id", std::string());
            diag.message = d.value("message", std::string());
            (diag.error ? errors : warnings)++;
            std::string where = diag.file.empty() ? std::string()
                                                  : std::format("{}({},{}): ", Platform::Utf8ToPath(diag.file).filename().string(),
                                                                diag.line, diag.column);
            std::string text = std::format("{}{} {}: {}", where, diag.error ? "error" : "warning", diag.id, diag.message);
            Log::Write(diag.error ? LogLevel::Error : LogLevel::Warning, std::move(text));
            m_Diagnostics.push_back(std::move(diag));
        }
    }
    m_HasErrors = state != 2;
    if (m_HasErrors) {
        Log::Error("Scripts have compile errors ({}). Fix them to enter Play Mode.", errors);
        return CompileState::Failed;
    }
    RefreshTypes();
    ++m_Version;
    Log::Info("Scripts compiled in {:.2f} s: {} MonoBehaviour class(es){}", result.value("seconds", 0.0),
              m_Types.size(), warnings ? std::format(", {} warning(s)", warnings) : std::string());
    return CompileState::Succeeded;
}

void ScriptEngine::RefreshTypes()
{
    g_Managed.DescribeScripts();
    m_Types = json::parse(TakeResult(), nullptr, false);
    if (!m_Types.is_array())
        m_Types = json::array();
}

const nlohmann::json* ScriptEngine::FindScriptType(const std::string& name) const
{
    for (const json& t : m_Types)
        if (t.value("name", std::string()) == name || t.value("fullName", std::string()) == name)
            return &t;
    return nullptr;
}

bool ScriptEngine::LoadAssembly(const std::filesystem::path& dll)
{
    if (!m_Available || !g_Managed.LoadAssembly(Platform::PathToUtf8(dll).c_str()))
        return false;
    RefreshTypes();
    ++m_Version;
    m_HasErrors = false;
    return true;
}

bool ScriptEngine::BeginPlay(Scene& scene, PhysicsWorld* physics)
{
    if (!m_Available)
        return false;
    g_Scene = &scene;
    g_Physics = physics;
    json doc;
    json& entities = doc["entities"] = json::array();
    for (const auto& e : scene.Entities()) {
        if (e->scripts.empty())
            continue;
        json scripts = json::array();
        for (const ScriptComponent& s : e->scripts) {
            json fields = json::parse(s.fields, nullptr, false);
            scripts.push_back({{"class", s.className}, {"enabled", s.enabled}, {"fields", fields.is_object() ? fields : json::object()}});
        }
        entities.push_back({{"id", e->id}, {"scripts", std::move(scripts)}});
    }
    m_Playing = true;
    g_Managed.BeginPlay(doc.dump().c_str());
    scene.UpdateWorldTransforms();
    return true;
}

void ScriptEngine::EndPlay()
{
    if (!m_Playing)
        return;
    g_Managed.EndPlay();
    Input::SetCursorLocked(false);
    if (g_Scene && E(g_TemplateRoot)) {
        if (g_Physics)
            for (EntityID id : g_Scene->Subtree(g_TemplateRoot))
                g_Physics->RemoveEntity(id);
        g_Scene->DestroyEntity(g_TemplateRoot);
    }
    g_TemplateRoot = 0;
    g_Templates.clear();
    m_Playing = false;
    g_Scene = nullptr;
    g_Physics = nullptr;
}

void ScriptEngine::FixedUpdate(float fixedDeltaTime)
{
    if (m_Playing)
        g_Managed.FixedUpdate(fixedDeltaTime);
}

void ScriptEngine::DispatchContacts(const std::vector<ContactEvent>& events)
{
    static_assert(sizeof(ContactEvent) == 52, "ContactEvent layout is shared with IndeetsEngine.Interop.ContactEvent");
    if (m_Playing && !events.empty()) {
        g_Managed.DispatchContacts(events.data(), static_cast<int>(events.size()));
        g_Scene->UpdateWorldTransforms();
    }
}

void ScriptEngine::Update(float unscaledDeltaTime)
{
    if (!m_Playing)
        return;
    g_Managed.Update(unscaledDeltaTime);
    g_Scene->UpdateWorldTransforms();
}

void ScriptEngine::LateUpdate()
{
    if (!m_Playing)
        return;
    g_Managed.LateUpdate();
    g_Scene->UpdateWorldTransforms();
}

float ScriptEngine::TimeScale() { return m_Playing ? std::max(0.0f, g_Managed.GetTimeScale()) : 1.0f; }

bool ScriptEngine::ConsumeQuitRequest() { return m_Playing && g_Managed.ConsumeQuitRequest() != 0; }

void ScriptEngine::DestroyEntity(EntityID id)
{
    if (m_Playing)
        g_Managed.DestroyEntity(id);
}

nlohmann::json ScriptEngine::EntityScripts(EntityID id)
{
    if (!m_Playing)
        return json::array();
    g_Managed.GetEntityScripts(id);
    json result = json::parse(TakeResult(), nullptr, false);
    return result.is_array() ? result : json::array();
}

bool ScriptEngine::SetScriptField(EntityID id, int index, const std::string& field, const nlohmann::json& value)
{
    return m_Playing && g_Managed.SetScriptField(id, index, field.c_str(), value.dump().c_str()) != 0;
}

bool ScriptEngine::SetScriptEnabled(EntityID id, int index, bool enabled)
{
    return m_Playing && g_Managed.SetScriptEnabled(id, index, enabled ? 1 : 0) != 0;
}

bool ScriptEngine::AddScript(EntityID id, const std::string& className, const std::string& fieldsJson)
{
    return m_Playing && g_Managed.AddScript(id, className.c_str(), fieldsJson.c_str()) != 0;
}

bool ScriptEngine::RemoveScript(EntityID id, int index)
{
    return m_Playing && g_Managed.RemoveScript(id, index) != 0;
}

void ScriptEngine::SetViewport(glm::vec2 origin, glm::vec2 size, bool inputEnabled)
{
    g_ViewportOrigin = origin;
    g_ViewportSize = glm::max(size, glm::vec2(1.0f));
    g_InputEnabled = inputEnabled;
}

} // namespace ie
