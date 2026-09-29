#pragma once

#include <cstdint>
#include <string>

struct GLFWwindow;

namespace ze {

struct WindowDesc {
    std::string title = "ZEngine";
    uint32_t width = 1600;
    uint32_t height = 900;
};

class Window {
public:
    explicit Window(const WindowDesc& desc);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool ShouldClose() const;
    void RequestClose();
    void PollEvents();
    void WaitEvents();
    void SetTitle(const std::string& title);

    // Size of the drawable area in pixels (0 when minimized).
    void GetFramebufferSize(uint32_t& width, uint32_t& height) const;
    bool IsMinimized() const;

    // Returns true once after the framebuffer was resized.
    bool ConsumeResized();

    GLFWwindow* Handle() const { return m_Window; }

private:
    GLFWwindow* m_Window = nullptr;
    bool m_Resized = false;
};

} // namespace ze
