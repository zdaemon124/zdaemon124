#include "ZEngine/Core/Application.h"

#include "ZEngine/Core/Input.h"
#include "ZEngine/Core/Log.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdlib>

namespace ze {

Application::Application(const ApplicationDesc& desc, int argc, char** argv)
{
    for (int i = 0; i < argc; ++i)
        m_Args.emplace_back(argv[i]);
    if (std::string frames = ArgValue("--frames"); !frames.empty())
        m_ExitAfterFrames = std::strtoull(frames.c_str(), nullptr, 10);

    m_Window = std::make_unique<Window>(desc.window);
    m_Renderer = std::make_unique<Renderer>(*m_Window, desc.renderer);
}

Application::~Application()
{
    m_Renderer->WaitIdle();
    m_Renderer.reset();
    m_Window.reset();
}

std::string Application::ArgValue(const std::string& name) const
{
    for (size_t i = 0; i + 1 < m_Args.size(); ++i)
        if (m_Args[i] == name)
            return m_Args[i + 1];
    return {};
}

void Application::Quit() { m_QuitRequested = true; }

void Application::Run()
{
    OnStart();

    double last = glfwGetTime();
    while (!m_QuitRequested) {
        Input::Detail::BeginFrame();
        m_Window->PollEvents();
        Input::Detail::EndPolling();

        if (m_Window->ShouldClose()) {
            glfwSetWindowShouldClose(m_Window->Handle(), GLFW_FALSE);
            if (OnCloseRequested())
                break;
        }
        if (m_Window->IsMinimized()) {
            m_Window->WaitEvents();
            last = glfwGetTime();
            continue;
        }

        double now = glfwGetTime();
        float realDelta = static_cast<float>(now - last);
        last = now;
        m_DeltaTime = std::min(realDelta, 0.1f); // avoid huge steps after stalls
        m_Time += m_DeltaTime;
        m_Renderer->SetTime(m_Time);

        OnUpdate(m_DeltaTime);
        OnRender();
        ++m_FrameCount;

        m_FpsTimer += realDelta;
        ++m_FpsFrames;
        if (m_FpsTimer >= 0.5f) {
            m_Fps = float(m_FpsFrames) / m_FpsTimer;
            m_FpsTimer = 0.0f;
            m_FpsFrames = 0;
        }
        if (m_ExitAfterFrames && m_FrameCount >= m_ExitAfterFrames)
            Quit();
    }

    m_Renderer->WaitIdle();
    OnShutdown();
}

} // namespace ze
