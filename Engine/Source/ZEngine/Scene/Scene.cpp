#include "ZEngine/Scene/Scene.h"

#include <algorithm>

namespace ze {

Entity& Scene::CreateEntity(const std::string& name)
{
    auto entity = std::make_unique<Entity>();
    entity->id = m_NextID++;
    entity->name = name;
    m_Entities.push_back(std::move(entity));
    return *m_Entities.back();
}

void Scene::DestroyEntity(EntityID id)
{
    std::erase_if(m_Entities, [id](const auto& e) { return e->id == id; });
}

Entity* Scene::Find(const std::string& name)
{
    auto it = std::find_if(m_Entities.begin(), m_Entities.end(), [&](const auto& e) { return e->name == name; });
    return it != m_Entities.end() ? it->get() : nullptr;
}

Entity* Scene::Get(EntityID id)
{
    auto it = std::find_if(m_Entities.begin(), m_Entities.end(), [id](const auto& e) { return e->id == id; });
    return it != m_Entities.end() ? it->get() : nullptr;
}

} // namespace ze
