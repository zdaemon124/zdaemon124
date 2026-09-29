#include "EditorUI.h"

#include <glm/gtc/type_ptr.hpp>

#include <cmath>

namespace ze::UI {

void Tooltip(const char* text)
{
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("%s", text);
}

void DrawIcon(ImDrawList* d, Icon icon, ImVec2 min, ImVec2 max, ImU32 col)
{
    const ImVec2 c((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
    const float r = std::min(max.x - min.x, max.y - min.y) * 0.5f;
    const float t = std::max(1.5f, r * 0.14f); // line thickness
    switch (icon) {
    case Icon::Move: {
        float a = r * 0.8f, h = r * 0.3f;
        d->AddLine({c.x - a, c.y}, {c.x + a, c.y}, col, t);
        d->AddLine({c.x, c.y - a}, {c.x, c.y + a}, col, t);
        d->AddTriangleFilled({c.x + a + 1, c.y}, {c.x + a - h, c.y - h}, {c.x + a - h, c.y + h}, col);
        d->AddTriangleFilled({c.x - a - 1, c.y}, {c.x - a + h, c.y - h}, {c.x - a + h, c.y + h}, col);
        d->AddTriangleFilled({c.x, c.y - a - 1}, {c.x - h, c.y - a + h}, {c.x + h, c.y - a + h}, col);
        d->AddTriangleFilled({c.x, c.y + a + 1}, {c.x - h, c.y + a - h}, {c.x + h, c.y + a - h}, col);
        break;
    }
    case Icon::Rotate: {
        float a = r * 0.65f;
        d->PathArcTo(c, a, 0.4f, 5.6f, 20);
        d->PathStroke(col, t);
        ImVec2 tip(c.x + a * std::cos(0.4f), c.y + a * std::sin(0.4f));
        d->AddTriangleFilled({tip.x - r * 0.3f, tip.y}, {tip.x + r * 0.3f, tip.y}, {tip.x, tip.y + r * 0.35f}, col);
        break;
    }
    case Icon::Scale: {
        float a = r * 0.75f;
        d->AddRect({c.x - a, c.y - a}, {c.x + a, c.y + a}, col, 1.0f, t);
        d->AddRectFilled({c.x, c.y - a}, {c.x + a, c.y}, col);
        break;
    }
    case Icon::Play: {
        float a = r * 0.6f;
        d->AddTriangleFilled({c.x - a * 0.7f, c.y - a}, {c.x - a * 0.7f, c.y + a}, {c.x + a, c.y}, col);
        break;
    }
    case Icon::Pause: {
        float a = r * 0.55f, w = r * 0.22f;
        d->AddRectFilled({c.x - a * 0.6f - w, c.y - a}, {c.x - a * 0.6f + w, c.y + a}, col);
        d->AddRectFilled({c.x + a * 0.6f - w, c.y - a}, {c.x + a * 0.6f + w, c.y + a}, col);
        break;
    }
    case Icon::Step: {
        float a = r * 0.55f;
        d->AddTriangleFilled({c.x - a, c.y - a}, {c.x - a, c.y + a}, {c.x + a * 0.5f, c.y}, col);
        d->AddRectFilled({c.x + a * 0.6f, c.y - a}, {c.x + a, c.y + a}, col);
        break;
    }
    case Icon::Folder: {
        ImVec2 a(min.x + (max.x - min.x) * 0.1f, min.y + (max.y - min.y) * 0.22f);
        ImVec2 b(max.x - (max.x - min.x) * 0.1f, max.y - (max.y - min.y) * 0.16f);
        float tab = (b.x - a.x) * 0.4f;
        d->AddRectFilled({a.x, a.y}, {a.x + tab, a.y + (b.y - a.y) * 0.2f}, IM_COL32(200, 160, 70, 255), 3.0f);
        d->AddRectFilled({a.x, a.y + (b.y - a.y) * 0.12f}, b, IM_COL32(230, 190, 90, 255), 3.0f);
        break;
    }
    case Icon::File:
    case Icon::Scene: {
        float w = (max.x - min.x) * 0.62f, h = (max.y - min.y) * 0.8f;
        ImVec2 a(c.x - w * 0.5f, c.y - h * 0.5f), b(c.x + w * 0.5f, c.y + h * 0.5f);
        float fold = w * 0.3f;
        ImU32 fill = icon == Icon::Scene ? IM_COL32(90, 140, 210, 255) : IM_COL32(190, 190, 190, 255);
        d->AddRectFilled(a, {b.x - fold, b.y}, fill, 2.0f);
        d->AddRectFilled({b.x - fold, a.y + fold}, b, fill, 2.0f);
        d->AddTriangleFilled({b.x - fold, a.y}, {b.x, a.y + fold}, {b.x - fold, a.y + fold}, IM_COL32(230, 230, 230, 255));
        if (icon == Icon::Scene) {
            ImVec2 m((a.x + b.x) * 0.5f, (a.y + b.y) * 0.55f);
            float s = w * 0.22f;
            d->AddTriangleFilled({m.x - s, m.y + s}, {m.x, m.y - s}, {m.x + s, m.y + s}, IM_COL32(255, 255, 255, 220));
        }
        break;
    }
    case Icon::Cube: {
        float a = r * 0.7f;
        ImVec2 top(c.x, c.y - a), left(c.x - a, c.y - a * 0.45f), right(c.x + a, c.y - a * 0.45f);
        ImVec2 mid(c.x, c.y + a * 0.05f), bl(c.x - a, c.y + a * 0.55f), br(c.x + a, c.y + a * 0.55f),
            bottom(c.x, c.y + a);
        d->AddQuadFilled(top, right, mid, left, IM_COL32(170, 190, 215, 255));
        d->AddQuadFilled(left, mid, bottom, bl, IM_COL32(120, 140, 170, 255));
        d->AddQuadFilled(mid, right, br, bottom, IM_COL32(95, 115, 145, 255));
        break;
    }
    case Icon::Light: {
        ImU32 sun = IM_COL32(250, 210, 80, 255);
        d->AddCircleFilled(c, r * 0.38f, sun);
        for (int i = 0; i < 8; ++i) {
            float ang = float(i) * 0.785398f;
            ImVec2 dir(std::cos(ang), std::sin(ang));
            d->AddLine({c.x + dir.x * r * 0.55f, c.y + dir.y * r * 0.55f},
                       {c.x + dir.x * r * 0.85f, c.y + dir.y * r * 0.85f}, sun, t * 0.8f);
        }
        break;
    }
    case Icon::Camera: {
        ImU32 cam = IM_COL32(150, 190, 240, 255);
        float a = r * 0.75f;
        d->AddRectFilled({c.x - a, c.y - a * 0.5f}, {c.x + a * 0.35f, c.y + a * 0.5f}, cam, 2.0f);
        d->AddTriangleFilled({c.x + a * 0.35f, c.y}, {c.x + a, c.y - a * 0.5f}, {c.x + a, c.y + a * 0.5f}, cam);
        break;
    }
    case Icon::Empty:
        d->AddCircle(c, r * 0.45f, col, 12, t * 0.8f);
        break;
    }
}

bool IconButton(const char* id, Icon icon, bool active, const char* tooltip, ImVec2 size)
{
    ImGui::PushID(id);
    if (active)
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    bool pressed = ImGui::Button("##icon", size);
    if (active)
        ImGui::PopStyleColor();
    ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
    ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
    ImVec2 pad(size.y * 0.2f, size.y * 0.2f);
    DrawIcon(ImGui::GetWindowDrawList(), icon, {min.x + pad.x, min.y + pad.y}, {max.x - pad.x, max.y - pad.y}, color);
    ImGui::PopID();
    if (tooltip)
        Tooltip(tooltip);
    return pressed;
}

bool BeginProperties(const char* id)
{
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX))
        return false;
    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch, 0.38f);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 0.62f);
    return true;
}

void EndProperties() { ImGui::EndTable(); }

void PropertyLabel(const char* label)
{
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
}

bool PropertyFloat(const char* label, float& value, float speed, float min, float max, const char* format)
{
    PropertyLabel(label);
    ImGui::PushID(label);
    bool changed = ImGui::DragFloat("##v", &value, speed, min, max, format);
    ImGui::PopID();
    return changed;
}

bool PropertyVec3(const char* label, glm::vec3& value, float speed, const char* format)
{
    PropertyLabel(label);
    ImGui::PushID(label);
    bool changed = false;
    const char* names[] = {"X", "Y", "Z"};
    const ImU32 colors[] = {IM_COL32(200, 70, 70, 255), IM_COL32(90, 170, 70, 255), IM_COL32(70, 110, 210, 255)};
    float full = ImGui::GetContentRegionAvail().x;
    float labelWidth = ImGui::CalcTextSize("X").x + 6.0f;
    float fieldWidth = (full - labelWidth * 3.0f - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
    for (int i = 0; i < 3; ++i) {
        if (i > 0)
            ImGui::SameLine();
        ImVec2 p = ImGui::GetCursorScreenPos();
        float h = ImGui::GetFrameHeight();
        ImGui::GetWindowDrawList()->AddRectFilled(p, {p.x + labelWidth, p.y + h}, colors[i], 2.0f);
        ImGui::GetWindowDrawList()->AddText({p.x + 3.0f, p.y + ImGui::GetStyle().FramePadding.y},
                                            IM_COL32_WHITE, names[i]);
        ImGui::Dummy({labelWidth, h});
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::SetNextItemWidth(fieldWidth);
        ImGui::PushID(i);
        changed |= ImGui::DragFloat("##c", &value[i], speed, 0.0f, 0.0f, format);
        ImGui::PopID();
    }
    ImGui::PopID();
    return changed;
}

bool PropertyBool(const char* label, bool& value)
{
    PropertyLabel(label);
    ImGui::PushID(label);
    bool changed = ImGui::Checkbox("##v", &value);
    ImGui::PopID();
    return changed;
}

bool PropertyColor(const char* label, glm::vec4& value)
{
    PropertyLabel(label);
    ImGui::PushID(label);
    bool changed = ImGui::ColorEdit4("##v", glm::value_ptr(value), ImGuiColorEditFlags_AlphaBar);
    ImGui::PopID();
    return changed;
}

bool PropertyColor(const char* label, glm::vec3& value)
{
    PropertyLabel(label);
    ImGui::PushID(label);
    bool changed = ImGui::ColorEdit3("##v", glm::value_ptr(value));
    ImGui::PopID();
    return changed;
}

bool PropertyText(const char* label, std::string& value)
{
    PropertyLabel(label);
    ImGui::PushID(label);
    bool changed = ImGui::InputText("##v", &value);
    ImGui::PopID();
    return changed;
}

bool PropertyCombo(const char* label, int& index, const char* const* items, int count)
{
    PropertyLabel(label);
    ImGui::PushID(label);
    bool changed = ImGui::Combo("##v", &index, items, count);
    ImGui::PopID();
    return changed;
}

} // namespace ze::UI
