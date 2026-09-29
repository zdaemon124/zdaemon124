#pragma once

#include <glm/glm.hpp>

namespace ze {

struct UIRect;
struct SceneSettings;

// Screen-space rectangle in pixels, origin at the top-left corner.
struct ScreenRect {
    glm::vec2 min{0.0f};
    glm::vec2 max{0.0f};
    glm::vec2 Size() const { return max - min; }
    bool Contains(glm::vec2 p) const { return p.x >= min.x && p.y >= min.y && p.x <= max.x && p.y <= max.y; }
};

namespace UILayout {
// "Scale With Screen Size" (match height), like Unity's Canvas Scaler.
float Scale(const SceneSettings& settings, glm::vec2 screenSize);
ScreenRect Compute(const UIRect& rect, glm::vec2 screenSize, float scale);
} // namespace UILayout

} // namespace ze
