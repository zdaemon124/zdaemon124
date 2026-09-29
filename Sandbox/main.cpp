// Runtime demo without the editor: primitives with physics.
// Controls: hold RMB + WASD/QE to fly (Shift = faster), MMB drag to pan, wheel to zoom,
//           Space shoots a ball, R restarts, Esc quits.
#include <IndeetsEngine/IndeetsEngine.h>

using namespace ie;

class SandboxApp final : public Application {
public:
    using Application::Application;

protected:
    void OnStart() override { BuildScene(); }

    void OnUpdate(float deltaTime) override
    {
        if (Input::GetKeyDown(Key::Escape))
            Quit();
        if (Input::GetKeyDown(Key::R))
            BuildScene();
        if (Input::GetKeyDown(Key::Space))
            ShootBall();

        m_Camera.Update(SmoothDeltaTime());
        m_Physics.Update(m_Scene, deltaTime);
    }

    void OnRender() override
    {
        m_Scene.UpdateWorldTransforms();
        GetRenderer().RenderToScreen(m_Scene, m_Camera.Data(GetRenderer().ScreenAspectRatio()));
    }

private:
    void BuildScene()
    {
        m_Physics.Stop();
        m_Scene.Clear();

        Entity& sun = m_Scene.CreateEntity("Directional Light");
        sun.light = LightComponent{};
        sun.transform.SetEulerAngles({50.0f, -30.0f, 0.0f});

        Entity& ground = m_Scene.CreatePrimitive(PrimitiveType::Plane, "Ground");
        ground.transform.scale = glm::vec3(3.0f);
        ground.meshRenderer->color = {0.72f, 0.72f, 0.72f, 1.0f};
        ground.meshRenderer->checkerScale = 1.0f;

        const glm::vec4 colors[] = {{0.9f, 0.3f, 0.25f, 1}, {0.3f, 0.55f, 0.95f, 1}, {0.35f, 0.8f, 0.4f, 1},
                                    {0.98f, 0.75f, 0.2f, 1}, {0.7f, 0.4f, 0.9f, 1}};
        // A pyramid of boxes.
        int index = 0;
        for (int row = 0; row < 5; ++row)
            for (int i = 0; i < 5 - row; ++i) {
                Entity& box = m_Scene.CreatePrimitive(PrimitiveType::Cube, "Box");
                box.transform.position = {-2.0f + float(i) + row * 0.5f, 0.5f + float(row), 3.0f};
                box.meshRenderer->color = colors[index++ % 5];
                box.rigidbody = RigidbodyComponent{};
            }

        Entity& capsule = m_Scene.CreatePrimitive(PrimitiveType::Capsule);
        capsule.transform.position = {4.0f, 3.0f, 0.0f};
        capsule.transform.SetEulerAngles({0.0f, 0.0f, 35.0f});
        capsule.meshRenderer->color = colors[2];
        capsule.rigidbody = RigidbodyComponent{};

        Entity& hint = m_Scene.CreateEntity("Hint");
        hint.uiText = UITextComponent{};
        hint.uiText->text = "Space - throw a ball    R - restart    RMB + WASD - fly";
        hint.uiText->fontSize = 28.0f;
        hint.rectTransform = RectTransform::Anchored({0.5f, 1.0f}, {0.0f, -20.0f}, {1200.0f, 50.0f});

        m_Physics.Start(m_Scene);
    }

    void ShootBall()
    {
        Entity& ball = m_Scene.CreatePrimitive(PrimitiveType::Sphere, "Ball");
        ball.transform.position = m_Camera.transform.position + m_Camera.transform.Forward();
        ball.meshRenderer->color = {0.95f, 0.95f, 0.95f, 1.0f};
        ball.rigidbody = RigidbodyComponent{};
        ball.rigidbody->mass = 5.0f;
        m_Physics.AddEntity(ball);
        m_Physics.SetLinearVelocity(ball.id, m_Camera.transform.Forward() * 25.0f);
    }

    Scene m_Scene;
    EditorCamera m_Camera;
    PhysicsWorld m_Physics;
};

int main(int argc, char** argv)
{
    ApplicationDesc desc;
    desc.window.title = "IndeetsEngine Sandbox";
    SandboxApp app(desc, argc, argv);
    app.Run();
    return 0;
}
