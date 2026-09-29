#include "ZEngine/Core/Application.h"

#include "ZEngine/Core/Input.h"
#include "ZEngine/Core/Log.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdlib>
#include <format>
#include <string_view>

namespace ze {

Application::Application(const ApplicationDesc& desc, int argc, char** argv)
    : m_Title(desc.window.title)
{
    for (int i = 1; i + 1 < argc; ++i)
        if (std::string_view(argv[i]) == "--frames")
            m_ExitAfterFrames = std::strtoull(argv[i + 1], nullptr, 10);

    m_Window = std::make_unique<Window>(desc.window);
    m_Renderer = std::make_unique<Renderer>(*m_Window, desc.renderer);
    m_Scene = std::make_unique<Scene>();
}

Application::~Application()
{
    m_Renderer->WaitIdle();
    m_Scene.reset();
    m_Renderer.reset();
    m_Window.reset();
}

Entity& Application::CreatePrimitive(PrimitiveType type, const std::string& name)
{
    Entity& entity = m_Scene->CreateEntity(name.empty() ? PrimitiveName(type) : name);
    entity.meshRenderer.mesh = m_Renderer->GetPrimitive(type);
    return entity;
}

void Application::Quit() { m_Window->RequestClose(); }

void Application::Run()
{
    OnStart();

    double last = glfwGetTime();
    while (!m_Window->ShouldClose()) {
        Input::Detail::BeginFrame();
        m_Window->PollEvents();
        Input::Detail::EndPolling();

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

        m_EditorCamera.Update(m_DeltaTime);
        OnUpdate(m_DeltaTime);

        m_Renderer->Render(*m_Scene, m_EditorCamera.Data(m_Renderer->AspectRatio()), m_Time);
        ++m_FrameCount;
        UpdateTitle(realDelta);

        if (m_ExitAfterFrames && m_FrameCount >= m_ExitAfterFrames)
            Quit();
    }

    m_Renderer->WaitIdle();
    OnShutdown();
}

void Application::UpdateTitle(float realDelta)
{
    m_FpsTimer += realDelta;
    ++m_FpsFrames;
    if (m_FpsTimer >= 0.5f) {
        float fps = float(m_FpsFrames) / m_FpsTimer;
        m_Window->SetTitle(std::format("{} | {:.0f} FPS ({:.2f} ms)", m_Title, fps, 1000.0f / fps));
        m_FpsTimer = 0.0f;
        m_FpsFrames = 0;
    }
}

} // namespace ze
