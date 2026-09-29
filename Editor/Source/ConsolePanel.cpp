#include "EditorApp.h"

#include "EditorUI.h"

namespace ie {

void EditorApp::DrawConsole()
{
    if (!ImGui::Begin("Console")) {
        ImGui::End();
        return;
    }

    // Copy the log only when it changed.
    uint64_t version = Log::Version();
    bool changed = version != m_ConsoleSeenVersion;
    if (changed) {
        m_ConsoleCache.clear();
        m_ConsoleCounts[0] = m_ConsoleCounts[1] = m_ConsoleCounts[2] = 0;
        Log::ForEachEntry([&](const LogEntry& e) {
            m_ConsoleCache.push_back(e);
            ++m_ConsoleCounts[int(e.level)];
        });
    }

    if (ImGui::Button("Clear"))
        Log::Clear();
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &m_ConsoleAutoScroll);
    ImGui::SameLine(0.0f, 20.0f);
    ImGui::Checkbox(std::format("Info ({})", m_ConsoleCounts[0]).c_str(), &m_ConsoleShowInfo);
    ImGui::SameLine();
    ImGui::Checkbox(std::format("Warnings ({})", m_ConsoleCounts[1]).c_str(), &m_ConsoleShowWarnings);
    ImGui::SameLine();
    ImGui::Checkbox(std::format("Errors ({})", m_ConsoleCounts[2]).c_str(), &m_ConsoleShowErrors);
    ImGui::Separator();

    std::vector<int> visible;
    visible.reserve(m_ConsoleCache.size());
    for (int i = 0; i < int(m_ConsoleCache.size()); ++i) {
        LogLevel level = m_ConsoleCache[i].level;
        if ((level == LogLevel::Info && m_ConsoleShowInfo) || (level == LogLevel::Warning && m_ConsoleShowWarnings) ||
            (level == LogLevel::Error && m_ConsoleShowErrors))
            visible.push_back(i);
    }

    ImGui::BeginChild("log", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    ImGuiListClipper clipper; // only the visible lines are submitted
    clipper.Begin(int(visible.size()));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const LogEntry& e = m_ConsoleCache[visible[row]];
            ImVec4 color = e.level == LogLevel::Error     ? ImVec4(1.0f, 0.4f, 0.35f, 1.0f)
                           : e.level == LogLevel::Warning ? ImVec4(1.0f, 0.8f, 0.3f, 1.0f)
                                                          : ImGui::GetStyleColorVec4(ImGuiCol_Text);
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextUnformatted(e.message.c_str());
            ImGui::PopStyleColor();
        }
    }
    if (m_ConsoleAutoScroll && changed)
        ImGui::SetScrollHereY(1.0f);
    m_ConsoleSeenVersion = version;
    ImGui::EndChild();
    ImGui::End();
}

} // namespace ie
