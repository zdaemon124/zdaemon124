#pragma once

#include "ZEngine/Core/Window.h"
#include "ZEngine/Renderer/Renderer.h"
#include "ZEngine/Scene/Camera.h"
#include "ZEngine/Scene/Primitives.h"
#include "ZEngine/Scene/Scene.h"

#include <memory>

namespace ze {

struct ApplicationDesc {
    WindowDesc window;
    RendererSettings renderer;
};

// Base class for engine apps: owns the window, renderer, scene and main loop.
// Command line: --frames N   quit automatically after N frames (automated tests).
class Application {
public:
    Application(const ApplicationDesc& desc, int argc = 0, char** argv = nullptr);
    virtual ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void Run();
    void Quit();

    // Equivalent of Unity's GameObject.CreatePrimitive.
    Entity& CreatePrimitive(PrimitiveType type, const std::string& name = {});

    Window& GetWindow() { return *m_Window; }
    Renderer& GetRenderer() { return *m_Renderer; }
    Scene& GetScene() { return *m_Scene; }
    EditorCamera& GetEditorCamera() { return m_EditorCamera; }

    float Time() const { return m_Time; }
    float DeltaTime() const { return m_DeltaTime; }
    uint64_t FrameCount() const { return m_FrameCount; }

protected:
    virtual void OnStart() {}
    virtual void OnUpdate(float /*deltaTime*/) {}
    virtual void OnShutdown() {}

private:
    void UpdateTitle(float realDelta);

    std::string m_Title;
    std::unique_ptr<Window> m_Window;
    std::unique_ptr<Renderer> m_Renderer;
    std::unique_ptr<Scene> m_Scene;
    EditorCamera m_EditorCamera;

    float m_Time = 0.0f;
    float m_DeltaTime = 0.0f;
    uint64_t m_FrameCount = 0;
    uint64_t m_ExitAfterFrames = 0;

    float m_FpsTimer = 0.0f;
    uint32_t m_FpsFrames = 0;
};

} // namespace ze
