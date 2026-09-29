#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace ze {

struct RectTransform;
struct SceneSettings;
class Scene;
struct Entity;

// Screen-space rectangle in pixels, origin at the top-left corner.
struct ScreenRect {
    glm::vec2 min{0.0f};
    glm::vec2 max{0.0f};
    glm::vec2 Size() const { return max - min; }
    glm::vec2 Center() const { return (min + max) * 0.5f; }
    bool Contains(glm::vec2 p) const { return p.x >= min.x && p.y >= min.y && p.x <= max.x && p.y <= max.y; }
};

// Rectangle in canvas units (reference pixels), origin at the bottom-left corner, y up.
struct CanvasRect {
    glm::vec2 min{0.0f};
    glm::vec2 max{0.0f};
    glm::vec2 Size() const { return max - min; }
};

namespace UILayout {

// Canvas Scaler: pixels per reference unit for a given screen size.
float Scale(const SceneSettings& settings, glm::vec2 screenSize);

// Element rect inside its parent (both in canvas units).
CanvasRect Resolve(const RectTransform& rect, const CanvasRect& parent);
// Inverse of Resolve: updates position/size so the element covers `target` (anchors and pivot are kept).
void SetFromCanvasRect(RectTransform& rect, const CanvasRect& target, const CanvasRect& parent);
// Changes anchors while keeping the element where it is on screen.
void SetAnchorsKeepRect(RectTransform& rect, glm::vec2 anchorMin, glm::vec2 anchorMax, const CanvasRect& parent);

} // namespace UILayout

// Resolved layout of every UI element of a scene for one screen size.
// Also provides the draw order (parents before children, siblings by `order`).
class UILayoutResult {
public:
    void Build(const Scene& scene, glm::vec2 screenSize);

    float Scale() const { return m_Scale; }
    glm::vec2 ScreenSize() const { return m_Screen; }
    CanvasRect CanvasBounds() const { return {{0.0f, 0.0f}, m_Screen / m_Scale}; }

    const CanvasRect* Find(uint32_t entity) const;
    ScreenRect ToScreen(const CanvasRect& r) const;
    glm::vec2 ScreenToCanvas(glm::vec2 screenPoint) const;
    // Canvas rect of the element's parent (the whole canvas for root elements).
    CanvasRect ParentRect(const Entity& entity) const;

    // Visible UI entities in draw order (back to front).
    const std::vector<const Entity*>& DrawOrder() const { return m_Order; }

private:
    float m_Scale = 1.0f;
    glm::vec2 m_Screen{1.0f};
    std::unordered_map<uint32_t, CanvasRect> m_Rects;
    std::vector<const Entity*> m_Order;
};

} // namespace ze
