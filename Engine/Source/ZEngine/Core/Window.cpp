#include "ZEngine/Core/Window.h"

#include "ZEngine/Core/Input.h"
#include "ZEngine/Core/Log.h"

#include <GLFW/glfw3.h>

namespace ze {

Window::Window(const WindowDesc& desc)
{
    glfwSetErrorCallback([](int code, const char* message) { Log::Error("GLFW {}: {}", code, message); });
    if (!glfwInit())
        Log::Fatal("Failed to initialize GLFW");
    if (!glfwVulkanSupported())
        Log::Fatal("Vulkan is not supported on this system (update your GPU driver)");

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    m_Window = glfwCreateWindow(static_cast<int>(desc.width), static_cast<int>(desc.height),
                                desc.title.c_str(), nullptr, nullptr);
    if (!m_Window)
        Log::Fatal("Failed to create window");

    glfwSetWindowUserPointer(m_Window, this);
    glfwSetFramebufferSizeCallback(m_Window, [](GLFWwindow* w, int, int) {
        static_cast<Window*>(glfwGetWindowUserPointer(w))->m_Resized = true;
    });
    Input::Detail::Attach(m_Window);
}

Window::~Window()
{
    glfwDestroyWindow(m_Window);
    glfwTerminate();
}

bool Window::ShouldClose() const { return glfwWindowShouldClose(m_Window); }
void Window::RequestClose() { glfwSetWindowShouldClose(m_Window, GLFW_TRUE); }
void Window::PollEvents() { glfwPollEvents(); }
void Window::WaitEvents() { glfwWaitEvents(); }
void Window::SetTitle(const std::string& title) { glfwSetWindowTitle(m_Window, title.c_str()); }

void Window::GetFramebufferSize(uint32_t& width, uint32_t& height) const
{
    int w = 0, h = 0;
    glfwGetFramebufferSize(m_Window, &w, &h);
    width = static_cast<uint32_t>(w);
    height = static_cast<uint32_t>(h);
}

bool Window::IsMinimized() const
{
    uint32_t w = 0, h = 0;
    GetFramebufferSize(w, h);
    return w == 0 || h == 0;
}

bool Window::ConsumeResized()
{
    bool resized = m_Resized;
    m_Resized = false;
    return resized;
}

} // namespace ze
