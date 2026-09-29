#include "ZEngine/Scene/Scene.h"

#include <algorithm>
#include <vector>

namespace ze {

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
    Entity copy = *Get(id);
    Entity& e = CreateEntity(copy.name);
    EntityID newId = e.id;
    e = copy;
    e.id = newId;
    // Place right after the original, like Unity.
    Move(newId, IndexOf(id) + 1);
    return *Get(newId);
}

void Scene::DestroyEntity(EntityID id)
{
    // UI children go with their parent.
    std::vector<EntityID> children;
    for (const auto& e : m_Entities)
        if (e->rectTransform && e->rectTransform->parent == id)
            children.push_back(e->id);
    for (EntityID child : children)
        DestroyEntity(child);
    std::erase_if(m_Entities, [id](const auto& e) { return e->id == id; });
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
    m_NextID = 1;
    settings = {};
}

Entity* Scene::Get(EntityID id)
{
    for (auto& e : m_Entities)
        if (e->id == id)
            return e.get();
    return nullptr;
}

const Entity* Scene::Get(EntityID id) const
{
    for (const auto& e : m_Entities)
        if (e->id == id)
            return e.get();
    return nullptr;
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

} // namespace ze
