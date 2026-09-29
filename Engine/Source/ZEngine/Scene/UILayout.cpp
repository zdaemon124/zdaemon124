#include "ZEngine/Scene/UILayout.h"

#include "ZEngine/Scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace ze {
namespace UILayout {

float Scale(const SceneSettings& settings, glm::vec2 screenSize)
{
    if (settings.uiScaleMode == UIScaleMode::ConstantPixelSize)
        return 1.0f;
    glm::vec2 reference = glm::max(settings.uiReferenceResolution, glm::vec2(1.0f));
    // Logarithmic blend between matching width and height (same as Unity).
    float match = std::clamp(settings.uiMatchWidthOrHeight, 0.0f, 1.0f);
    float logWidth = std::log2(std::max(screenSize.x, 1.0f) / reference.x);
    float logHeight = std::log2(std::max(screenSize.y, 1.0f) / reference.y);
    return std::exp2(logWidth + (logHeight - logWidth) * match);
}

CanvasRect Resolve(const RectTransform& rect, const CanvasRect& parent)
{
    glm::vec2 parentSize = parent.Size();
    glm::vec2 anchorMin = parent.min + rect.anchorMin * parentSize;
    glm::vec2 anchorMax = parent.min + rect.anchorMax * parentSize;
    glm::vec2 size = (anchorMax - anchorMin) + rect.size;
    glm::vec2 pivotPoint = anchorMin + (anchorMax - anchorMin) * rect.pivot + rect.position;
    CanvasRect r;
    r.min = pivotPoint - rect.pivot * size;
    r.max = r.min + size;
    return r;
}

void SetFromCanvasRect(RectTransform& rect, const CanvasRect& target, const CanvasRect& parent)
{
    glm::vec2 parentSize = parent.Size();
    glm::vec2 anchorMin = parent.min + rect.anchorMin * parentSize;
    glm::vec2 anchorMax = parent.min + rect.anchorMax * parentSize;
    glm::vec2 size = target.Size();
    rect.size = size - (anchorMax - anchorMin);
    glm::vec2 pivotPoint = target.min + rect.pivot * size;
    rect.position = pivotPoint - (anchorMin + (anchorMax - anchorMin) * rect.pivot);
}

void SetAnchorsKeepRect(RectTransform& rect, glm::vec2 anchorMin, glm::vec2 anchorMax, const CanvasRect& parent)
{
    CanvasRect current = Resolve(rect, parent);
    rect.anchorMin = anchorMin;
    rect.anchorMax = anchorMax;
    SetFromCanvasRect(rect, current, parent);
}

} // namespace UILayout

void UILayoutResult::Build(const Scene& scene, glm::vec2 screenSize)
{
    m_Screen = glm::max(screenSize, glm::vec2(1.0f));
    m_Scale = std::max(UILayout::Scale(scene.settings, m_Screen), 0.0001f);
    m_Rects.clear();
    m_Order.clear();

    // Children grouped by parent (0 = screen); unknown parents fall back to the screen.
    std::unordered_map<uint32_t, std::vector<std::pair<int, const Entity*>>> children;
    const auto& entities = scene.Entities();
    for (size_t i = 0; i < entities.size(); ++i) {
        const Entity& e = *entities[i];
        if (!e.rectTransform)
            continue;
        uint32_t parent = e.rectTransform->parent;
        const Entity* p = parent ? scene.Get(parent) : nullptr;
        if (!p || !p->rectTransform || parent == e.id)
            parent = 0;
        children[parent].push_back({int(i), &e});
    }

    std::function<void(uint32_t, const CanvasRect&, bool, int)> visit = [&](uint32_t parent, const CanvasRect& parentRect,
                                                                             bool visible, int depth) {
        auto it = children.find(parent);
        if (it == children.end() || depth > 64)
            return;
        auto list = it->second;
        std::stable_sort(list.begin(), list.end(), [](const auto& a, const auto& b) {
            return a.second->rectTransform->order < b.second->rectTransform->order;
        });
        for (const auto& [index, e] : list) {
            if (m_Rects.contains(e->id))
                continue; // cycle guard
            CanvasRect r = UILayout::Resolve(*e->rectTransform, parentRect);
            m_Rects[e->id] = r;
            bool shown = visible && e->active;
            if (shown)
                m_Order.push_back(e);
            visit(e->id, r, shown, depth + 1);
        }
    };
    visit(0, CanvasBounds(), true, 0);
}

const CanvasRect* UILayoutResult::Find(uint32_t entity) const
{
    auto it = m_Rects.find(entity);
    return it != m_Rects.end() ? &it->second : nullptr;
}

ScreenRect UILayoutResult::ToScreen(const CanvasRect& r) const
{
    ScreenRect s;
    s.min = {r.min.x * m_Scale, m_Screen.y - r.max.y * m_Scale};
    s.max = {r.max.x * m_Scale, m_Screen.y - r.min.y * m_Scale};
    return s;
}

glm::vec2 UILayoutResult::ScreenToCanvas(glm::vec2 p) const
{
    return {p.x / m_Scale, (m_Screen.y - p.y) / m_Scale};
}

CanvasRect UILayoutResult::ParentRect(const Entity& entity) const
{
    if (entity.rectTransform && entity.rectTransform->parent)
        if (const CanvasRect* r = Find(entity.rectTransform->parent))
            return *r;
    return CanvasBounds();
}

} // namespace ze
