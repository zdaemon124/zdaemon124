#pragma once

#include "ZEngine/Core/Window.h"
#include "ZEngine/Renderer/Renderer.h"

#include <memory>
#include <string>
#include <vector>

namespace ze {

struct ApplicationDesc {
    WindowDesc window;
    RendererSettings renderer;
};

// Base class for engine apps: owns the window, the renderer and the main loop.
// Command line: --frames N   quit automatically after N frames (automated tests).
class Application {
public:
    Application(const ApplicationDesc& desc, int argc = 0, char** argv = nullptr);
    virtual ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void Run();
    void Quit();

    Window& GetWindow() { return *m_Window; }
    Renderer& GetRenderer() { return *m_Renderer; }

    float Time() const { return m_Time; }
    float DeltaTime() const { return m_DeltaTime; }
    // Delta time averaged over recent frames: steadier motion for cameras and animation under vsync jitter.
    float SmoothDeltaTime() const { return m_SmoothDeltaTime; }
    float Fps() const { return m_Fps; }
    uint64_t FrameCount() const { return m_FrameCount; }
    const std::vector<std::string>& Args() const { return m_Args; }
    // Value following `name` on the command line, or empty.
    std::string ArgValue(const std::string& name) const;

protected:
    virtual void OnStart() {}
    virtual void OnUpdate(float /*deltaTime*/) {}
    virtual void OnRender() = 0;
    virtual void OnShutdown() {}
    // Return false to cancel closing the window (e.g. to ask about unsaved changes).
    virtual bool OnCloseRequested() { return true; }

private:
    std::vector<std::string> m_Args;
    std::unique_ptr<Window> m_Window;
    std::unique_ptr<Renderer> m_Renderer;

    float m_Time = 0.0f;
    float m_DeltaTime = 0.0f;
    float m_SmoothDeltaTime = 1.0f / 60.0f;
    float m_Fps = 0.0f;
    uint64_t m_FrameCount = 0;
    uint64_t m_ExitAfterFrames = 0;
    float m_FpsTimer = 0.0f;
    uint32_t m_FpsFrames = 0;
    bool m_QuitRequested = false;
};

} // namespace ze
