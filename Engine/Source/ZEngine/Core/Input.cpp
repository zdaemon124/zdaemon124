#include "ZEngine/Core/Input.h"

#include <GLFW/glfw3.h>

#include <array>

namespace ze::Input {
namespace {

struct State {
    GLFWwindow* window = nullptr;
    std::array<bool, GLFW_KEY_LAST + 1> keys{};
    std::array<bool, GLFW_KEY_LAST + 1> prevKeys{};
    std::array<bool, GLFW_MOUSE_BUTTON_LAST + 1> buttons{};
    std::array<bool, GLFW_MOUSE_BUTTON_LAST + 1> prevButtons{};
    glm::vec2 mousePos{0.0f};
    glm::vec2 lastMousePos{0.0f};
    glm::vec2 mouseDelta{0.0f};
    bool firstMouse = true;
    float scroll = 0.0f;
};

State g;

bool ValidKey(int key) { return key >= 0 && key <= GLFW_KEY_LAST; }
bool ValidButton(int b) { return b >= 0 && b <= GLFW_MOUSE_BUTTON_LAST; }

void KeyCallback(GLFWwindow*, int key, int, int action, int)
{
    if (!ValidKey(key) || action == GLFW_REPEAT)
        return;
    g.keys[key] = action == GLFW_PRESS;
}

void MouseButtonCallback(GLFWwindow*, int button, int action, int)
{
    if (ValidButton(button))
        g.buttons[button] = action == GLFW_PRESS;
}

void ScrollCallback(GLFWwindow*, double, double yOffset)
{
    g.scroll += static_cast<float>(yOffset);
}

} // namespace

bool GetKey(Key key) { return ValidKey(int(key)) && g.keys[int(key)]; }
bool GetKeyDown(Key key) { return ValidKey(int(key)) && g.keys[int(key)] && !g.prevKeys[int(key)]; }
bool GetKeyUp(Key key) { return ValidKey(int(key)) && !g.keys[int(key)] && g.prevKeys[int(key)]; }

bool GetMouseButton(MouseButton b) { return g.buttons[int(b)]; }
bool GetMouseButtonDown(MouseButton b) { return g.buttons[int(b)] && !g.prevButtons[int(b)]; }
bool GetMouseButtonUp(MouseButton b) { return !g.buttons[int(b)] && g.prevButtons[int(b)]; }

glm::vec2 MousePosition() { return g.mousePos; }
glm::vec2 MouseDelta() { return g.mouseDelta; }
float ScrollDelta() { return g.scroll; }

void SetCursorLocked(bool locked)
{
    if (g.window)
        glfwSetInputMode(g.window, GLFW_CURSOR, locked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}

namespace Detail {

void Attach(GLFWwindow* window)
{
    g = State{};
    g.window = window;
    glfwSetKeyCallback(window, KeyCallback);
    glfwSetMouseButtonCallback(window, MouseButtonCallback);
    glfwSetScrollCallback(window, ScrollCallback);
}

void BeginFrame()
{
    g.prevKeys = g.keys;
    g.prevButtons = g.buttons;
    g.scroll = 0.0f;
}

void EndPolling()
{
    double x = 0.0, y = 0.0;
    glfwGetCursorPos(g.window, &x, &y);
    g.mousePos = {static_cast<float>(x), static_cast<float>(y)};
    if (g.firstMouse) {
        g.lastMousePos = g.mousePos;
        g.firstMouse = false;
    }
    g.mouseDelta = g.mousePos - g.lastMousePos;
    g.lastMousePos = g.mousePos;
}

} // namespace Detail
} // namespace ze::Input
