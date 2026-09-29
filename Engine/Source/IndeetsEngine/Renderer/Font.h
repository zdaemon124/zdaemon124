#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ie {

struct Texture;
class Renderer;

// Glyph atlas baked from a TrueType font (Latin + Cyrillic).
class Font {
public:
    static constexpr float kBakeSize = 48.0f; // pixels; text is scaled from this size

    bool Load(Renderer& renderer, const std::filesystem::path& path);

    struct Glyph {
        glm::vec2 min, max;     // quad relative to the pen position (pixels at bake size, y down)
        glm::vec2 uvMin, uvMax;
        float advance = 0.0f;
    };

    const Glyph* Find(uint32_t codepoint) const;
    float LineHeight() const { return m_LineHeight; }
    float Ascent() const { return m_Ascent; }
    // Width of the widest line, in pixels at bake size.
    float MeasureWidth(std::string_view utf8) const;
    Texture* Atlas() const { return m_Atlas; }

    static std::vector<uint32_t> DecodeUtf8(std::string_view text);

private:
    struct Range {
        uint32_t first;
        std::vector<Glyph> glyphs;
    };
    std::vector<Range> m_Ranges;
    Texture* m_Atlas = nullptr;
    float m_LineHeight = kBakeSize;
    float m_Ascent = kBakeSize * 0.8f;
};

} // namespace ie
