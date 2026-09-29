#include "ZEngine/Scene/UILayout.h"

#include "ZEngine/Scene/Scene.h"

namespace ze::UILayout {

float Scale(const SceneSettings& settings, glm::vec2 screenSize)
{
    float reference = settings.uiReferenceResolution.y > 0.0f ? settings.uiReferenceResolution.y : 1080.0f;
    return screenSize.y / reference;
}

ScreenRect Compute(const UIRect& rect, glm::vec2 screenSize, float scale)
{
    // UI coordinates are Unity-like: anchors/pivots in 0..1 from the bottom-left, position in reference pixels (y up).
    glm::vec2 size = rect.size * scale;
    glm::vec2 pivotPoint = rect.anchor * screenSize + rect.position * scale;
    glm::vec2 minBottomUp = pivotPoint - rect.pivot * size;
    glm::vec2 maxBottomUp = minBottomUp + size;
    ScreenRect r;
    r.min = {minBottomUp.x, screenSize.y - maxBottomUp.y};
    r.max = {maxBottomUp.x, screenSize.y - minBottomUp.y};
    return r;
}

} // namespace ze::UILayout
