#include "ZEngine/Renderer/Texture.h"

#include "ZEngine/Core/Platform.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO_FILENAME_WIDE_WARNINGS
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>

namespace ze::ImageIO {

bool IsImageFile(const std::filesystem::path& path)
{
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp";
}

bool Load(const std::filesystem::path& path, ImageData& out)
{
    // Read through std::filesystem so non-ASCII paths work on Windows.
    std::vector<char> bytes = Platform::ReadBinaryFile(path);
    if (bytes.empty())
        return false;
    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()), int(bytes.size()), &w, &h,
                                            &channels, 4);
    if (!pixels)
        return false;
    out.width = uint32_t(w);
    out.height = uint32_t(h);
    out.pixels.assign(pixels, pixels + size_t(w) * size_t(h) * 4);
    stbi_image_free(pixels);
    return true;
}

bool SavePng(const std::filesystem::path& path, const ImageData& image)
{
    int length = 0;
    unsigned char* png = stbi_write_png_to_mem(image.pixels.data(), int(image.width * 4), int(image.width),
                                               int(image.height), 4, &length);
    if (!png)
        return false;
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(png), length);
    STBIW_FREE(png);
    return bool(file);
}

} // namespace ze::ImageIO
