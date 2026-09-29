// Demo scene: all built-in primitives on a ground plane.
// Controls: hold RMB + WASD/QE to fly (Shift = faster), MMB drag to pan, wheel to zoom,
//           Space pauses the animation, Esc quits.
#include <ZEngine/ZEngine.h>

#include <cmath>

using namespace ze;

class SandboxApp final : public Application {
public:
    using Application::Application;

protected:
    void OnStart() override
    {
        Entity& ground = CreatePrimitive(PrimitiveType::Plane, "Ground");
        ground.transform.scale = glm::vec3(3.0f);
        ground.meshRenderer.color = {0.45f, 0.45f, 0.45f, 1.0f};
        ground.meshRenderer.checkerScale = 1.0f;

        struct Item { PrimitiveType type; glm::vec3 position; glm::vec4 color; };
        const Item items[] = {
            {PrimitiveType::Cube,     {-4.5f, 0.5f, 0.0f}, {0.75f, 0.06f, 0.04f, 1.0f}},
            {PrimitiveType::Sphere,   {-1.5f, 0.5f, 0.0f}, {0.04f, 0.25f, 0.8f, 1.0f}},
            {PrimitiveType::Capsule,  { 1.5f, 1.0f, 0.0f}, {0.06f, 0.5f, 0.1f, 1.0f}},
            {PrimitiveType::Cylinder, { 4.5f, 1.0f, 0.0f}, {0.9f, 0.45f, 0.03f, 1.0f}},
        };
        for (const Item& item : items) {
            Entity& e = CreatePrimitive(item.type);
            e.transform.position = item.position;
            e.meshRenderer.color = item.color;
        }

        Entity& quad = CreatePrimitive(PrimitiveType::Quad, "Quad");
        quad.transform.position = {0.0f, 1.5f, 4.0f};
        quad.transform.scale = glm::vec3(3.0f);
        quad.meshRenderer.color = {0.8f, 0.8f, 0.85f, 1.0f};

        // A small tower of cubes that will be dropped by physics in a later milestone.
        for (int i = 0; i < 5; ++i) {
            Entity& cube = CreatePrimitive(PrimitiveType::Cube, "Stack " + std::to_string(i));
            cube.transform.position = {-3.0f, 0.5f + float(i), 4.0f};
            cube.transform.SetEulerAngles({0.0f, float(i) * 12.0f, 0.0f});
            cube.meshRenderer.color = {0.8f, 0.8f, 0.8f - float(i) * 0.15f, 1.0f};
        }

        m_Spinner = &CreatePrimitive(PrimitiveType::Cube, "Spinner");
        m_Spinner->transform.position = {3.0f, 1.5f, 4.0f};
        m_Spinner->meshRenderer.color = {0.4f, 0.08f, 0.7f, 1.0f};
    }

    void OnUpdate(float deltaTime) override
    {
        if (Input::GetKeyDown(Key::Escape))
            Quit();
        if (Input::GetKeyDown(Key::Space))
            m_Paused = !m_Paused;
        if (m_Paused)
            return;

        m_AnimTime += deltaTime;
        m_Spinner->transform.Rotate(glm::vec3(20.0f, 45.0f, 0.0f) * deltaTime);
        m_Spinner->transform.position.y = 1.5f + std::sin(m_AnimTime * 2.0f) * 0.4f;
    }

private:
    Entity* m_Spinner = nullptr;
    float m_AnimTime = 0.0f;
    bool m_Paused = false;
};

int main(int argc, char** argv)
{
    ApplicationDesc desc;
    desc.window.title = "ZEngine Sandbox";
    SandboxApp app(desc, argc, argv);
    app.Run();
    return 0;
}
