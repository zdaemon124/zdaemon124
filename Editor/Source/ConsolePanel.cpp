#include "EditorApp.h"

#include "EditorUI.h"

namespace ze {

void EditorApp::DrawConsole()
{
    if (!ImGui::Begin("Console")) {
        ImGui::End();
        return;
    }

    int counts[3] = {0, 0, 0};
    Log::ForEachEntry([&](const LogEntry& e) { ++counts[int(e.level)]; });

    if (ImGui::Button("Clear"))
        Log::Clear();
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &m_ConsoleAutoScroll);
    ImGui::SameLine(0.0f, 20.0f);
    ImGui::Checkbox(std::format("Info ({})", counts[0]).c_str(), &m_ConsoleShowInfo);
    ImGui::SameLine();
    ImGui::Checkbox(std::format("Warnings ({})", counts[1]).c_str(), &m_ConsoleShowWarnings);
    ImGui::SameLine();
    ImGui::Checkbox(std::format("Errors ({})", counts[2]).c_str(), &m_ConsoleShowErrors);
    ImGui::Separator();

    ImGui::BeginChild("log", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    Log::ForEachEntry([&](const LogEntry& e) {
        ImVec4 color;
        switch (e.level) {
        case LogLevel::Info:
            if (!m_ConsoleShowInfo) return;
            color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
            break;
        case LogLevel::Warning:
            if (!m_ConsoleShowWarnings) return;
            color = ImVec4(1.0f, 0.8f, 0.3f, 1.0f);
            break;
        case LogLevel::Error:
            if (!m_ConsoleShowErrors) return;
            color = ImVec4(1.0f, 0.4f, 0.35f, 1.0f);
            break;
        }
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextUnformatted(e.message.c_str());
        ImGui::PopStyleColor();
    });
    uint64_t version = Log::Version();
    if (m_ConsoleAutoScroll && version != m_ConsoleSeenVersion)
        ImGui::SetScrollHereY(1.0f);
    m_ConsoleSeenVersion = version;
    ImGui::EndChild();
    ImGui::End();
}

} // namespace ze
