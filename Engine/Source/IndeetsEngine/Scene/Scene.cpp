#include "IndeetsEngine/Scene/Scene.h"
#include "IndeetsEngine/Scene/SceneSerializer.h"

#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ie {

const char* ColliderShapeName(ColliderShape shape)
{
    switch (shape) {
    case ColliderShape::Box: return "Box";
    case ColliderShape::Sphere: return "Sphere";
    case ColliderShape::Capsule: return "Capsule";
    }
    return "Unknown";
}

Entity& Scene::CreateEntity(const std::string& name, EntityID id)
{
    auto entity = std::make_unique<Entity>();
    if (id == kInvalidEntity || Get(id))
        id = m_NextID;
    entity->id = id;
    m_NextID = std::max(m_NextID, id + 1);
    entity->name = name;
    m_Index[id] = entity.get();
    m_Entities.push_back(std::move(entity));
    return *m_Entities.back();
}

Entity& Scene::CreatePrimitive(PrimitiveType type, const std::string& name)
{
    Entity& e = CreateEntity(name.empty() ? PrimitiveName(type) : name);
    e.meshRenderer = MeshRendererComponent{};
    e.meshRenderer->mesh = PrimitiveName(type);

    ColliderComponent collider;
    switch (type) {
    case PrimitiveType::Sphere: collider.shape = ColliderShape::Sphere; break;
    case PrimitiveType::Capsule:
    case PrimitiveType::Cylinder: collider.shape = ColliderShape::Capsule; break;
    case PrimitiveType::Plane:
        collider.size = {10.0f, 0.02f, 10.0f};
        collider.center = {0.0f, -0.01f, 0.0f};
        break;
    case PrimitiveType::Quad: collider.size = {1.0f, 1.0f, 0.02f}; break;
    default: break;
    }
    e.collider = collider;
    return e;
}

Entity& Scene::Duplicate(EntityID id)
{
    std::vector<EntityID> subtree = Subtree(id);
    std::unordered_map<EntityID, EntityID> remap;
    int insertAt = IndexOf(subtree.back()) + 1;
    for (EntityID oldId : subtree) {
        Entity copy = *Get(oldId);
        Entity& e = CreateEntity(copy.name);
        EntityID newId = e.id;
        e = copy;
        e.id = newId;
        remap[oldId] = newId;
    }
    for (EntityID oldId : subtree) {
        Entity& e = *Get(remap[oldId]);
        if (auto it = remap.find(e.parent); it != remap.end())
            e.parent = it->second;
        if (e.rectTransform)
            if (auto it = remap.find(e.rectTransform->parent); it != remap.end())
                e.rectTransform->parent = it->second;
        // Script references inside the copied subtree point at the copies, like Unity.
        for (ScriptComponent& script : e.scripts)
            SceneSerializer::RemapEntityReferences(script.fields, remap, false);
        // Place the copies right after the original subtree, like Unity.
        Move(e.id, insertAt++);
    }
    return *Get(remap[id]);
}

void Scene::DestroyEntity(EntityID id)
{
    std::vector<EntityID> subtree = Subtree(id);
    std::unordered_set<EntityID> doomed(subtree.begin(), subtree.end());
    std::erase_if(m_Entities, [&](const auto& e) { return doomed.contains(e->id); });
    for (EntityID doomedId : doomed)
        m_Index.erase(doomedId);
}

bool Scene::IsActiveInHierarchy(EntityID id) const
{
    for (int guard = 0; id != kInvalidEntity && guard < 1024; ++guard) {
        const Entity* e = Get(id);
        if (!e || !e->active)
            return false;
        id = e->parent ? e->parent : (e->rectTransform ? e->rectTransform->parent : kInvalidEntity);
    }
    return true;
}

std::vector<EntityID> Scene::Children(EntityID id) const
{
    std::vector<EntityID> result;
    for (const auto& e : m_Entities)
        if (e->id != id && (e->parent == id || (e->rectTransform && e->rectTransform->parent == id)))
            result.push_back(e->id);
    return result;
}

std::vector<EntityID> Scene::Subtree(EntityID root) const
{
    std::vector<EntityID> result;
    if (!Get(root))
        return result;
    std::unordered_set<EntityID> seen;
    std::vector<EntityID> stack{root};
    while (!stack.empty()) {
        EntityID id = stack.back();
        stack.pop_back();
        if (!seen.insert(id).second)
            continue;
        result.push_back(id);
        std::vector<EntityID> children = Children(id);
        for (auto it = children.rbegin(); it != children.rend(); ++it)
            stack.push_back(*it);
    }
    return result;
}

bool Scene::IsDescendant(EntityID id, EntityID ancestor) const
{
    for (int guard = 0; id != 0 && guard < 1024; ++guard) {
        if (id == ancestor)
            return true;
        const Entity* e = Get(id);
        id = e ? e->parent : 0;
    }
    return false;
}

glm::mat4 Scene::ParentWorld(const Entity& entity) const
{
    const Entity* parent = entity.parent ? Get(entity.parent) : nullptr;
    return parent ? parent->world : glm::mat4(1.0f);
}

bool Scene::SetParent(EntityID child, EntityID parent, bool keepWorld)
{
    Entity* e = Get(child);
    if (!e || child == parent || (parent && IsDescendant(parent, child)))
        return false;
    UpdateWorldTransforms();
    glm::mat4 world = e->world;
    e->parent = parent;
    if (keepWorld)
        SetWorldMatrix(*e, world);
    UpdateWorldTransforms();
    return true;
}

void Scene::SetWorldMatrix(Entity& entity, const glm::mat4& world)
{
    glm::mat4 local = glm::inverse(ParentWorld(entity)) * world;
    glm::vec3 scale, translation, skew;
    glm::vec4 perspective;
    glm::quat rotation;
    if (glm::decompose(local, scale, rotation, translation, skew, perspective)) {
        entity.transform.position = translation;
        entity.transform.rotation = glm::normalize(rotation);
        entity.transform.scale = scale;
    }
    entity.world = ParentWorld(entity) * entity.transform.Matrix();
}

void Scene::UpdateWorldTransforms()
{
    // Parents are resolved on demand so the order of entities does not matter.
    std::unordered_map<EntityID, Entity*> byId;
    byId.reserve(m_Entities.size());
    for (auto& e : m_Entities)
        byId[e->id] = e.get();
    std::unordered_set<EntityID> done;
    done.reserve(m_Entities.size());
    std::vector<Entity*> chain;
    for (auto& start : m_Entities) {
        if (done.contains(start->id))
            continue;
        chain.clear();
        for (Entity* e = start.get(); e && !done.contains(e->id) && chain.size() < 1024;) {
            chain.push_back(e);
            auto it = e->parent ? byId.find(e->parent) : byId.end();
            e = it != byId.end() && it->second != e ? it->second : nullptr;
        }
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            Entity* e = *it;
            auto parent = e->parent ? byId.find(e->parent) : byId.end();
            glm::mat4 parentWorld = parent != byId.end() && parent->second != e && done.contains(parent->first)
                                        ? parent->second->world
                                        : glm::mat4(1.0f);
            e->world = parentWorld * e->transform.Matrix();
            done.insert(e->id);
        }
    }
}

bool Scene::IsUIDescendant(EntityID id, EntityID ancestor) const
{
    for (int guard = 0; id != 0 && guard < 256; ++guard) {
        if (id == ancestor)
            return true;
        const Entity* e = Get(id);
        id = e && e->rectTransform ? e->rectTransform->parent : 0;
    }
    return false;
}

void Scene::Clear()
{
    m_Entities.clear();
    m_Index.clear();
    m_NextID = 1;
    settings = {};
}

Entity* Scene::Get(EntityID id)
{
    auto it = m_Index.find(id);
    return it != m_Index.end() ? it->second : nullptr;
}

const Entity* Scene::Get(EntityID id) const
{
    auto it = m_Index.find(id);
    return it != m_Index.end() ? it->second : nullptr;
}

Entity* Scene::Find(const std::string& name)
{
    for (auto& e : m_Entities)
        if (e->name == name)
            return e.get();
    return nullptr;
}

int Scene::IndexOf(EntityID id) const
{
    for (size_t i = 0; i < m_Entities.size(); ++i)
        if (m_Entities[i]->id == id)
            return static_cast<int>(i);
    return -1;
}

void Scene::Move(EntityID id, int newIndex)
{
    int from = IndexOf(id);
    if (from < 0)
        return;
    newIndex = std::clamp(newIndex, 0, static_cast<int>(m_Entities.size()) - 1);
    auto entity = std::move(m_Entities[from]);
    m_Entities.erase(m_Entities.begin() + from);
    m_Entities.insert(m_Entities.begin() + newIndex, std::move(entity));
}

const Entity* Scene::MainLight() const
{
    for (const auto& e : m_Entities)
        if (e->active && e->light)
            return e.get();
    return nullptr;
}

const Entity* Scene::MainCamera() const
{
    for (const auto& e : m_Entities)
        if (e->active && e->camera)
            return e.get();
    return nullptr;
}

} // namespace ie
