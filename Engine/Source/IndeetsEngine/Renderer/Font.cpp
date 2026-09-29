#include "IndeetsEngine/Renderer/Font.h"

#include "IndeetsEngine/Core/Log.h"
#include "IndeetsEngine/Core/Platform.h"
#include "IndeetsEngine/Renderer/Renderer.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <algorithm>

namespace ie {

namespace {
constexpr int kAtlasSize = 1024;
struct CharRange {
    uint32_t first;
    uint32_t count;
};
constexpr CharRange kRanges[] = {
    {32, 95},     // Basic Latin
    {0xA0, 96},   // Latin-1 supplement
    {0x400, 96},  // Cyrillic
    {0x2010, 32}, // dashes, quotes, bullets, ellipsis
};
} // namespace

bool Font::Load(Renderer& renderer, const std::filesystem::path& path)
{
    std::vector<char> ttf = Platform::ReadBinaryFile(path);
    if (ttf.empty())
        return false;
    const auto* data = reinterpret_cast<const unsigned char*>(ttf.data());

    std::vector<unsigned char> alpha(size_t(kAtlasSize) * kAtlasSize);
    stbtt_pack_context pack;
    if (!stbtt_PackBegin(&pack, alpha.data(), kAtlasSize, kAtlasSize, 0, 1, nullptr))
        return false;
    stbtt_PackSetOversampling(&pack, 2, 2);

    std::vector<std::vector<stbtt_packedchar>> packed;
    for (const CharRange& r : kRanges) {
        packed.emplace_back(r.count);
        stbtt_PackFontRange(&pack, data, 0, kBakeSize, int(r.first), int(r.count), packed.back().data());
    }
    stbtt_PackEnd(&pack);

    stbtt_fontinfo info;
    if (stbtt_InitFont(&info, data, stbtt_GetFontOffsetForIndex(data, 0))) {
        int ascent = 0, descent = 0, gap = 0;
        stbtt_GetFontVMetrics(&info, &ascent, &descent, &gap);
        float scale = stbtt_ScaleForPixelHeight(&info, kBakeSize);
        m_Ascent = float(ascent) * scale;
        m_LineHeight = float(ascent - descent + gap) * scale;
    }

    m_Ranges.clear();
    for (size_t i = 0; i < std::size(kRanges); ++i) {
        Range range{kRanges[i].first, {}};
        for (uint32_t c = 0; c < kRanges[i].count; ++c) {
            float x = 0.0f, y = 0.0f;
            stbtt_aligned_quad q;
            stbtt_GetPackedQuad(packed[i].data(), kAtlasSize, kAtlasSize, int(c), &x, &y, &q, 0);
            range.glyphs.push_back({{q.x0, q.y0}, {q.x1, q.y1}, {q.s0, q.t0}, {q.s1, q.t1}, packed[i][c].xadvance});
        }
        m_Ranges.push_back(std::move(range));
    }

    // White RGB with coverage in alpha, so text uses the same shader as sprites.
    ImageData image;
    image.width = image.height = kAtlasSize;
    image.pixels.resize(alpha.size() * 4);
    for (size_t i = 0; i < alpha.size(); ++i) {
        image.pixels[i * 4 + 0] = 255;
        image.pixels[i * 4 + 1] = 255;
        image.pixels[i * 4 + 2] = 255;
        image.pixels[i * 4 + 3] = alpha[i];
    }
    m_Atlas = renderer.CreateTexture("FontAtlas:" + path.filename().string(), image);
    return m_Atlas != nullptr;
}

const Font::Glyph* Font::Find(uint32_t codepoint) const
{
    for (const Range& r : m_Ranges)
        if (codepoint >= r.first && codepoint < r.first + r.glyphs.size())
            return &r.glyphs[codepoint - r.first];
    return nullptr;
}

float Font::MeasureWidth(std::string_view utf8) const
{
    float width = 0.0f, line = 0.0f;
    for (uint32_t c : DecodeUtf8(utf8)) {
        if (c == '\n') {
            width = std::max(width, line);
            line = 0.0f;
            continue;
        }
        const Glyph* g = Find(c);
        if (!g)
            g = Find('?');
        if (g)
            line += g->advance;
    }
    return std::max(width, line);
}

std::vector<uint32_t> Font::DecodeUtf8(std::string_view text)
{
    std::vector<uint32_t> out;
    for (size_t i = 0; i < text.size();) {
        auto c = static_cast<unsigned char>(text[i]);
        size_t extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
        uint32_t cp = extra == 3 ? (c & 0x07u) : extra == 2 ? (c & 0x0Fu) : extra == 1 ? (c & 0x1Fu) : c;
        if (i + extra >= text.size() && extra > 0)
            break; // truncated sequence
        for (size_t k = 1; k <= extra; ++k)
            cp = (cp << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3Fu);
        out.push_back(cp);
        i += extra + 1;
    }
    return out;
}

} // namespace ie
