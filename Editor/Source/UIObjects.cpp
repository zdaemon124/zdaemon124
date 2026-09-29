#include "EditorApp.h"

#include <ZEngine/Core/Platform.h>

#include <algorithm>
#include <cmath>

namespace ze {

namespace fs = std::filesystem;

namespace {

float Smooth(float edge0, float edge1, float x)
{
    float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Signed distance to a rounded box centered at 0 (half size b, corner radius r).
float RoundedBox(glm::vec2 p, glm::vec2 b, float r)
{
    glm::vec2 q = glm::abs(p) - b + r;
    return glm::length(glm::max(q, glm::vec2(0.0f))) + std::min(std::max(q.x, q.y), 0.0f) - r;
}

template <class Shader>
ImageData Generate(uint32_t w, uint32_t h, Shader shader)
{
    ImageData img;
    img.width = w;
    img.height = h;
    img.pixels.resize(size_t(w) * h * 4);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            // p in -1..1, y up
            glm::vec2 p((float(x) + 0.5f) / float(w) * 2.0f - 1.0f, 1.0f - (float(y) + 0.5f) / float(h) * 2.0f);
            glm::vec4 c = glm::clamp(shader(p), glm::vec4(0.0f), glm::vec4(1.0f));
            uint8_t* px = &img.pixels[(size_t(y) * w + x) * 4];
            for (int i = 0; i < 4; ++i)
                px[i] = uint8_t(std::lround(c[i] * 255.0f));
        }
    return img;
}

glm::vec3 Gold(float t) { return glm::mix(glm::vec3(0.45f, 0.3f, 0.1f), glm::vec3(1.0f, 0.86f, 0.5f), t); }

// Diablo-style liquid orb with glass highlight and a gold rim.
ImageData Orb(glm::vec3 liquid)
{
    return Generate(256, 256, [&](glm::vec2 p) {
        float d = glm::length(p);
        float edge = 1.0f - Smooth(0.96f, 0.99f, d);
        glm::vec3 color = liquid * (1.15f - d * 0.8f);
        // Swirl.
        float swirl = std::sin(p.x * 7.0f + p.y * 3.0f) * std::sin(p.y * 6.0f - p.x * 2.0f);
        color += liquid * swirl * 0.08f;
        // Glass highlight.
        float hl = 1.0f - Smooth(0.0f, 0.45f, glm::length(p - glm::vec2(-0.3f, 0.4f)));
        color += glm::vec3(hl * 0.55f);
        // Gold rim.
        float rim = Smooth(0.82f, 0.86f, d) * (1.0f - Smooth(0.94f, 0.97f, d));
        color = glm::mix(color, Gold(0.5f + p.y * 0.5f), rim);
        return glm::vec4(color, edge);
    });
}

ImageData Slot()
{
    return Generate(128, 128, [](glm::vec2 p) {
        float outer = RoundedBox(p, glm::vec2(0.96f), 0.18f);
        float inner = RoundedBox(p, glm::vec2(0.8f), 0.1f);
        float alpha = 1.0f - Smooth(-0.02f, 0.02f, outer);
        glm::vec3 border = Gold(0.5f + (p.y - p.x) * 0.35f);
        glm::vec3 center = glm::vec3(0.06f, 0.05f, 0.05f) + glm::vec3(0.05f) * (1.0f - glm::length(p));
        float t = Smooth(-0.02f, 0.02f, inner);
        return glm::vec4(glm::mix(center, border, t), alpha * glm::mix(0.85f, 1.0f, t));
    });
}

ImageData Frame()
{
    return Generate(256, 256, [](glm::vec2 p) {
        float outer = RoundedBox(p, glm::vec2(0.98f), 0.12f);
        float inner = RoundedBox(p, glm::vec2(0.9f), 0.08f);
        float alpha = 1.0f - Smooth(-0.01f, 0.01f, outer);
        float t = Smooth(-0.01f, 0.01f, inner);
        glm::vec3 border = Gold(0.5f + p.y * 0.4f);
        glm::vec4 center(0.05f, 0.04f, 0.03f, 0.78f);
        return glm::vec4(glm::mix(glm::vec3(center), border, t), alpha * glm::mix(center.a, 1.0f, t));
    });
}

ImageData Panel()
{
    return Generate(256, 128, [](glm::vec2 p) {
        float d = RoundedBox(p, glm::vec2(0.97f, 0.94f), 0.2f);
        float alpha = (1.0f - Smooth(-0.02f, 0.02f, d)) * 0.8f;
        glm::vec3 c = glm::mix(glm::vec3(0.03f), glm::vec3(0.1f, 0.09f, 0.08f), p.y * 0.5f + 0.5f);
        return glm::vec4(c, alpha);
    });
}

} // namespace

void EditorApp::CreateSampleSprites()
{
    fs::path dir = AssetsDir() / "Sprites";
    std::error_code ec;
    if (fs::exists(dir, ec))
        return; // only once per project; the user may edit or delete them
    fs::create_directories(dir, ec);
    ImageIO::SavePng(dir / "Orb_Health.png", Orb({0.75f, 0.06f, 0.05f}));
    ImageIO::SavePng(dir / "Orb_Mana.png", Orb({0.08f, 0.2f, 0.85f}));
    ImageIO::SavePng(dir / "Slot.png", Slot());
    ImageIO::SavePng(dir / "Frame_Gold.png", Frame());
    ImageIO::SavePng(dir / "Panel_Dark.png", Panel());
    Log::Info("Created sample sprites in Assets/Sprites");
}

EntityID EditorApp::SelectedUIParent()
{
    Entity* e = Selected();
    return e && e->rectTransform ? e->id : 0;
}

Entity& EditorApp::CreateUIGroup(const std::string& name, EntityID parent)
{
    Entity& e = CreateObject(name);
    e.rectTransform = RectTransform{};
    e.rectTransform->parent = parent;
    e.rectTransform->size = {300.0f, 200.0f};
    return e;
}

Entity& EditorApp::CreateUIImage(const std::string& sprite, glm::vec2 position, EntityID parent)
{
    std::string name = sprite.empty() ? "Image" : Platform::Utf8ToPath(sprite).stem().string();
    Entity& e = CreateObject(name);
    e.rectTransform = RectTransform{};
    e.rectTransform->parent = parent;
    e.rectTransform->position = position;
    e.uiImage = UIImageComponent{};
    e.uiImage->sprite = sprite;
    if (Texture* tex = sprite.empty() ? nullptr : GetRenderer().LoadTexture(sprite))
        e.rectTransform->size = {float(tex->width), float(tex->height)};
    return e;
}

Entity& EditorApp::CreateUIText(const std::string& text, EntityID parent)
{
    Entity& e = CreateObject("Text");
    e.rectTransform = RectTransform{};
    e.rectTransform->parent = parent;
    e.rectTransform->size = {400.0f, 60.0f};
    e.uiText = UITextComponent{};
    e.uiText->text = text;
    return e;
}

void EditorApp::CreateSampleHUD()
{
    // Built with nested, anchored elements so it stays correct at any resolution / aspect ratio.
    const glm::vec4 goldText{1.0f, 0.85f, 0.45f, 1.0f};
    auto rect = [](glm::vec2 anchor, glm::vec2 pos, glm::vec2 size, int order = 0) {
        return RectTransform::Anchored(anchor, pos, size, order);
    };
    auto image = [&](const char* name, const char* sprite, RectTransform r, EntityID parent) -> Entity& {
        Entity& e = CreateObject(name);
        r.parent = parent;
        e.rectTransform = r;
        e.uiImage = UIImageComponent{};
        e.uiImage->sprite = sprite;
        return e;
    };
    auto text = [&](const char* name, const std::string& str, RectTransform r, EntityID parent, float fontSize,
                    glm::vec4 color) -> Entity& {
        Entity& e = CreateObject(name);
        r.parent = parent;
        e.rectTransform = r;
        e.uiText = UITextComponent{};
        e.uiText->text = str;
        e.uiText->fontSize = fontSize;
        e.uiText->color = color;
        return e;
    };

    // Root: stretches over the whole screen.
    Entity& hud = CreateObject("HUD");
    hud.rectTransform = RectTransform{};
    hud.rectTransform->anchorMin = {0, 0};
    hud.rectTransform->anchorMax = {1, 1};
    hud.rectTransform->size = {0, 0};
    EntityID root = hud.id;

    // Orbs in the bottom corners with centered values.
    Entity& health = image("Health Orb", "Sprites/Orb_Health.png", rect({0, 0}, {40, 30}, {230, 230}, 10), root);
    Entity& healthText = text("Value", "250 / 250", RectTransform{}, health.id, 26, {1, 1, 1, 1});
    healthText.rectTransform->anchorMin = {0, 0.4f};
    healthText.rectTransform->anchorMax = {1, 0.6f};
    healthText.rectTransform->size = {0, 0};
    Entity& mana = image("Mana Orb", "Sprites/Orb_Mana.png", rect({1, 0}, {-40, 30}, {230, 230}, 10), root);
    Entity& manaText = text("Value", "180 / 180", RectTransform{}, mana.id, 26, {1, 1, 1, 1});
    manaText.rectTransform->anchorMin = {0, 0.4f};
    manaText.rectTransform->anchorMax = {1, 0.6f};
    manaText.rectTransform->size = {0, 0};

    // Action bar: slots are anchored to fractions of the bar, so they spread with it.
    Entity& bar = image("Action Bar", "Sprites/Panel_Dark.png", rect({0.5f, 0}, {0, 16}, {760, 110}, 5), root);
    for (int i = 0; i < 8; ++i) {
        float x = (float(i) + 0.5f) / 8.0f;
        RectTransform slotRect = RectTransform::Anchored({x, 0.5f}, {0, 0}, {80, 80});
        slotRect.pivot = {0.5f, 0.5f};
        Entity& slot = image(("Slot " + std::to_string(i + 1)).c_str(), "Sprites/Slot.png", slotRect, bar.id);
        text("Hotkey", std::to_string(i + 1), rect({1, 1}, {-6, -4}, {24, 24}), slot.id, 20, goldText);
    }

    // Quest tracker in the top-right corner; title/body stretch across the frame width.
    Entity& frame = image("Quest Frame", "Sprites/Frame_Gold.png", rect({1, 1}, {-30, -30}, {380, 160}, 5), root);
    Entity& title = text("Title", "The Fallen Keep", RectTransform{}, frame.id, 30, goldText);
    title.rectTransform->anchorMin = {0, 1};
    title.rectTransform->anchorMax = {1, 1};
    title.rectTransform->pivot = {0.5f, 1};
    title.rectTransform->position = {0, -12};
    title.rectTransform->size = {-20, 44};
    Entity& body = text("Objective", "Slay the skeleton lord\n0 / 1", RectTransform{}, frame.id, 22, {0.9f, 0.9f, 0.9f, 1});
    body.rectTransform->anchorMin = {0, 0};
    body.rectTransform->anchorMax = {1, 1};
    body.rectTransform->position = {0, -20};
    body.rectTransform->size = {-30, -70};

    Select(root);
    m_FocusUIPanel = true;
    Log::Info("Created sample HUD (open the UI panel to edit it)");
}

} // namespace ze
