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

Entity& EditorApp::CreateUIImage(const std::string& sprite, glm::vec2 position)
{
    std::string name = sprite.empty() ? "Image" : Platform::Utf8ToPath(sprite).stem().string();
    Entity& e = CreateObject(name);
    e.uiImage = UIImageComponent{};
    e.uiImage->sprite = sprite;
    e.uiImage->rect.position = position;
    if (Texture* tex = sprite.empty() ? nullptr : GetRenderer().LoadTexture(sprite))
        e.uiImage->rect.size = {float(tex->width), float(tex->height)};
    return e;
}

Entity& EditorApp::CreateUIText(const std::string& text)
{
    Entity& e = CreateObject("Text");
    e.uiText = UITextComponent{};
    e.uiText->text = text;
    return e;
}

void EditorApp::CreateSampleHUD()
{
    const glm::vec4 goldText{1.0f, 0.85f, 0.45f, 1.0f};

    auto image = [&](const char* name, const char* sprite, glm::vec2 anchor, glm::vec2 pos, glm::vec2 size, int order) {
        Entity& e = CreateObject(name);
        e.uiImage = UIImageComponent{};
        e.uiImage->sprite = sprite;
        e.uiImage->rect = {anchor, anchor, pos, size, order};
        return &e;
    };
    auto text = [&](const char* name, const std::string& str, glm::vec2 anchor, glm::vec2 pos, glm::vec2 size,
                    float fontSize, glm::vec4 color, int order) {
        Entity& e = CreateObject(name);
        e.uiText = UITextComponent{};
        e.uiText->text = str;
        e.uiText->fontSize = fontSize;
        e.uiText->color = color;
        e.uiText->rect = {anchor, anchor, pos, size, order};
        return &e;
    };

    image("Health Orb", "Sprites/Orb_Health.png", {0, 0}, {40, 30}, {230, 230}, 10);
    text("Health Text", "250 / 250", {0, 0}, {40, 130}, {230, 40}, 26, {1, 1, 1, 1}, 11);
    image("Mana Orb", "Sprites/Orb_Mana.png", {1, 0}, {-40, 30}, {230, 230}, 10);
    text("Mana Text", "180 / 180", {1, 0}, {-40, 130}, {230, 40}, 26, {1, 1, 1, 1}, 11);

    Entity* bar = image("Action Bar", "Sprites/Panel_Dark.png", {0.5f, 0}, {0, 16}, {760, 110}, 5);
    bar->uiImage->rect.pivot = {0.5f, 0.0f};
    for (int i = 0; i < 8; ++i) {
        glm::vec2 pos{(float(i) - 3.5f) * 88.0f, 30.0f};
        Entity* slot = image(("Slot " + std::to_string(i + 1)).c_str(), "Sprites/Slot.png", {0.5f, 0}, pos, {80, 80}, 6);
        slot->uiImage->rect.pivot = {0.5f, 0.0f};
        Entity* key = text(("Hotkey " + std::to_string(i + 1)).c_str(), std::to_string(i + 1), {0.5f, 0},
                           pos + glm::vec2(24.0f, 52.0f), {30, 30}, 20, goldText, 7);
        key->uiText->rect.pivot = {0.5f, 0.0f};
    }

    Entity* frame = image("Quest Frame", "Sprites/Frame_Gold.png", {1, 1}, {-30, -30}, {380, 160}, 5);
    (void)frame;
    text("Quest Title", "The Fallen Keep", {1, 1}, {-30, -45}, {380, 44}, 30, goldText, 6);
    text("Quest Text", "Slay the skeleton lord\n0 / 1", {1, 1}, {-30, -95}, {380, 80}, 22, {0.9f, 0.9f, 0.9f, 1}, 6);
    Log::Info("Created sample HUD (see the Game view)");
}

} // namespace ze
