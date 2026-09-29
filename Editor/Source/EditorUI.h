#pragma once

#include <imgui.h>
#include <imgui_stdlib.h>

#include <glm/glm.hpp>

#include <string>

// Small immediate-mode helpers for a Unity-like look (icons, label/value property rows).
namespace ie::UI {

enum class Icon { Move, Rotate, Scale, Play, Pause, Step, Folder, File, Scene, Cube, Light, Camera, Empty, Image, Model, Prefab, Script };

void Tooltip(const char* text);
void DrawIcon(ImDrawList* draw, Icon icon, ImVec2 min, ImVec2 max, ImU32 color);
bool IconButton(const char* id, Icon icon, bool active, const char* tooltip, ImVec2 size = ImVec2(30.0f, 26.0f));

// Two-column property grid: label on the left, widget on the right.
bool BeginProperties(const char* id);
void EndProperties();
bool PropertyFloat(const char* label, float& value, float speed = 0.05f, float min = 0.0f, float max = 0.0f,
                   const char* format = "%.3f");
bool PropertyVec3(const char* label, glm::vec3& value, float speed = 0.05f, const char* format = "%.3f");
bool PropertyBool(const char* label, bool& value);
bool PropertyColor(const char* label, glm::vec4& value);
bool PropertyColor(const char* label, glm::vec3& value);
bool PropertyText(const char* label, std::string& value);
bool PropertyCombo(const char* label, int& index, const char* const* items, int count);
void PropertyLabel(const char* label);

} // namespace ie::UI
