#include "EditorApp.h"

#include "EditorUI.h"

#include <ZEngine/Core/Platform.h>

#include <algorithm>

namespace ze {

namespace fs = std::filesystem;

namespace {

struct DirEntry {
    fs::path path;
    bool directory;
};

std::vector<DirEntry> ListDirectory(const fs::path& dir)
{
    std::vector<DirEntry> entries;
    std::error_code ec;
    for (const auto& item : fs::directory_iterator(dir, ec)) {
        std::string name = item.path().filename().string();
        if (name.empty() || name[0] == '.')
            continue;
        entries.push_back({item.path(), item.is_directory(ec)});
    }
    std::sort(entries.begin(), entries.end(), [](const DirEntry& a, const DirEntry& b) {
        if (a.directory != b.directory)
            return a.directory;
        return a.path.filename().string() < b.path.filename().string();
    });
    return entries;
}

fs::path UniquePath(const fs::path& dir, const std::string& stem, const std::string& extension)
{
    fs::path candidate = dir / (stem + extension);
    for (int i = 1; fs::exists(candidate); ++i)
        candidate = dir / (stem + " " + std::to_string(i) + extension);
    return candidate;
}

} // namespace

void EditorApp::DrawProject()
{
    if (!ImGui::Begin("Project")) {
        ImGui::End();
        return;
    }
    const fs::path assets = AssetsDir();
    std::error_code ec;
    if (!fs::exists(m_ProjectCurrentDir, ec))
        m_ProjectCurrentDir = assets;
    fs::path pendingOpenScene;
    fs::path navigateTo;

    // ---- Top bar: back, breadcrumbs, icon size.
    ImGui::BeginDisabled(m_ProjectCurrentDir == assets);
    if (ImGui::ArrowButton("##back", ImGuiDir_Left))
        navigateTo = m_ProjectCurrentDir.parent_path();
    ImGui::EndDisabled();
    ImGui::SameLine();
    fs::path crumb = assets;
    if (ImGui::SmallButton("Assets"))
        navigateTo = assets;
    for (const auto& part : fs::relative(m_ProjectCurrentDir, assets, ec)) {
        if (part == ".")
            continue;
        crumb /= part;
        ImGui::SameLine(0.0f, 2.0f);
        ImGui::TextDisabled(">");
        ImGui::SameLine(0.0f, 2.0f);
        ImGui::PushID(crumb.string().c_str());
        if (ImGui::SmallButton(part.string().c_str()))
            navigateTo = crumb;
        ImGui::PopID();
    }
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 10.0f, ImGui::GetWindowWidth() - 150.0f));
    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderFloat("##iconsize", &m_ProjectIconSize, 40.0f, 128.0f, "");
    UI::Tooltip("Icon size");
    ImGui::Separator();

    if (ImGui::BeginTable("project", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("tree", ImGuiTableColumnFlags_WidthFixed, 190.0f);
        ImGui::TableSetupColumn("files", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();

        // ---- Left: folder tree.
        ImGui::TableNextColumn();
        ImGui::BeginChild("tree");
        std::function<void(const fs::path&)> drawFolder = [&](const fs::path& dir) {
            bool hasChildren = false;
            for (const auto& entry : ListDirectory(dir))
                if (entry.directory) { hasChildren = true; break; }
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (!hasChildren)
                flags |= ImGuiTreeNodeFlags_Leaf;
            if (dir == m_ProjectCurrentDir)
                flags |= ImGuiTreeNodeFlags_Selected;
            if (dir == assets)
                flags |= ImGuiTreeNodeFlags_DefaultOpen;
            bool open = ImGui::TreeNodeEx(dir.string().c_str(), flags, "%s", dir.filename().string().c_str());
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                navigateTo = dir;
            if (open) {
                for (const auto& entry : ListDirectory(dir))
                    if (entry.directory)
                        drawFolder(entry.path);
                ImGui::TreePop();
            }
        };
        drawFolder(assets);
        ImGui::EndChild();

        // ---- Right: icon grid of the current folder.
        ImGui::TableNextColumn();
        ImGui::BeginChild("files");
        const float cell = m_ProjectIconSize + 16.0f;
        int columns = std::max(1, int(ImGui::GetContentRegionAvail().x / cell));
        std::vector<DirEntry> entries = ListDirectory(m_ProjectCurrentDir);
        if (entries.empty())
            ImGui::TextDisabled("This folder is empty. Right-click to create a folder or a scene,\n"
                                "or drag files here from Windows Explorer.");

        if (ImGui::BeginTable("grid", columns)) {
            for (const DirEntry& entry : entries) {
                ImGui::TableNextColumn();
                ImGui::PushID(entry.path.string().c_str());
                std::string name = entry.directory ? entry.path.filename().string() : entry.path.stem().string();
                bool isScene = entry.path.extension() == ".zscene";

                ImVec2 pos = ImGui::GetCursorScreenPos();
                bool selected = entry.path == m_ScenePath;
                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.17f, 0.36f, 0.53f, 0.6f));
                ImGui::Selectable("##item", selected, ImGuiSelectableFlags_AllowDoubleClick,
                                  ImVec2(m_ProjectIconSize, m_ProjectIconSize));
                ImGui::PopStyleColor();
                bool hovered = ImGui::IsItemHovered();
                if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    if (entry.directory)
                        navigateTo = entry.path;
                    else if (isScene)
                        pendingOpenScene = entry.path;
                }
                bool isImage = !entry.directory && ImageIO::IsImageFile(entry.path);
                std::string assetPath = Platform::PathToUtf8(fs::relative(entry.path, assets, ec));
                if (!entry.directory && ImGui::BeginDragDropSource()) {
                    ImGui::SetDragDropPayload("ZE_ASSET", assetPath.c_str(), assetPath.size() + 1);
                    ImGui::TextUnformatted(assetPath.c_str());
                    ImGui::EndDragDropSource();
                }
                Texture* thumbnail = isImage ? GetRenderer().LoadTexture(assetPath) : nullptr;
                if (thumbnail) {
                    // Checkerboard behind the image so transparency is visible.
                    float pad = 4.0f, box = m_ProjectIconSize - pad * 2.0f;
                    float aspect = float(thumbnail->width) / float(thumbnail->height);
                    ImVec2 extent = aspect > 1.0f ? ImVec2(box, box / aspect) : ImVec2(box * aspect, box);
                    ImVec2 a(pos.x + (m_ProjectIconSize - extent.x) * 0.5f, pos.y + (m_ProjectIconSize - extent.y) * 0.5f);
                    ImDrawList* draw = ImGui::GetWindowDrawList();
                    draw->AddRectFilled(a, {a.x + extent.x, a.y + extent.y}, IM_COL32(80, 80, 80, 255));
                    draw->AddImage(ImTextureRef(m_ImGui->Texture(*thumbnail)), a, {a.x + extent.x, a.y + extent.y});
                } else {
                    UI::DrawIcon(ImGui::GetWindowDrawList(),
                                 entry.directory ? UI::Icon::Folder : isScene ? UI::Icon::Scene : UI::Icon::File, pos,
                                 {pos.x + m_ProjectIconSize, pos.y + m_ProjectIconSize}, IM_COL32_WHITE);
                }
                if (hovered)
                    UI::Tooltip(entry.path.filename().string().c_str());

                if (ImGui::BeginPopupContextItem("item")) {
                    if (ImGui::MenuItem("Open")) {
                        if (entry.directory)
                            navigateTo = entry.path;
                        else if (isScene)
                            pendingOpenScene = entry.path;
                    }
                    if (ImGui::MenuItem("Rename")) {
                        m_ProjectRenaming = entry.path;
                        m_ProjectRenameBuffer = name;
                    }
                    if (isImage && ImGui::MenuItem("Reimport"))
                        GetRenderer().UnloadTexture(assetPath);
                    if (ImGui::MenuItem("Delete"))
                        m_ProjectPendingDelete = entry.path;
                    ImGui::Separator();
                    if (ImGui::MenuItem("Show in Explorer"))
                        Platform::OpenInFileBrowser(entry.path);
                    ImGui::EndPopup();
                }

                // Label (or rename field).
                if (m_ProjectRenaming == entry.path) {
                    ImGui::SetNextItemWidth(m_ProjectIconSize);
                    ImGui::SetKeyboardFocusHere();
                    if (ImGui::InputText("##rename", &m_ProjectRenameBuffer,
                                         ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
                        fs::path target = entry.path.parent_path() /
                                          (m_ProjectRenameBuffer + (entry.directory ? "" : entry.path.extension().string()));
                        if (!m_ProjectRenameBuffer.empty() && !fs::exists(target)) {
                            fs::rename(entry.path, target, ec);
                            if (ec)
                                Log::Error("Rename failed: {}", ec.message());
                            else if (m_ScenePath == entry.path)
                                m_ScenePath = target;
                        }
                        m_ProjectRenaming.clear();
                    }
                    if (ImGui::IsKeyPressed(ImGuiKey_Escape))
                        m_ProjectRenaming.clear();
                } else {
                    std::string label = name;
                    float maxWidth = m_ProjectIconSize;
                    while (label.size() > 3 && ImGui::CalcTextSize(label.c_str()).x > maxWidth)
                        label = label.substr(0, label.size() - 4) + "..";
                    float offset = (maxWidth - ImGui::CalcTextSize(label.c_str()).x) * 0.5f;
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, offset));
                    ImGui::TextUnformatted(label.c_str());
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        // Background context menu.
        if (ImGui::BeginPopupContextWindow("files-context", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            if (ImGui::BeginMenu("Create")) {
                if (ImGui::MenuItem("Folder")) {
                    fs::path dir = UniquePath(m_ProjectCurrentDir, "New Folder", "");
                    fs::create_directory(dir, ec);
                    m_ProjectRenaming = dir;
                    m_ProjectRenameBuffer = dir.filename().string();
                }
                if (ImGui::MenuItem("Scene")) {
                    Scene empty;
                    Entity& cam = empty.CreateEntity("Main Camera");
                    cam.camera = CameraComponent{};
                    cam.transform.position = {0.0f, 1.0f, -10.0f};
                    Entity& light = empty.CreateEntity("Directional Light");
                    light.light = LightComponent{};
                    light.transform.SetEulerAngles({50.0f, -30.0f, 0.0f});
                    fs::path path = UniquePath(m_ProjectCurrentDir, "New Scene", ".zscene");
                    SceneSerializer::Save(empty, path);
                    m_ProjectRenaming = path;
                    m_ProjectRenameBuffer = path.stem().string();
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Save Current Scene Here", nullptr, false, !IsPlaying())) {
                std::string stem = m_ScenePath.empty() ? "Scene" : m_ScenePath.stem().string();
                SaveSceneAs(UniquePath(m_ProjectCurrentDir, stem, ".zscene"));
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Show in Explorer"))
                Platform::OpenInFileBrowser(m_ProjectCurrentDir);
            ImGui::EndPopup();
        }
        ImGui::EndChild();
        ImGui::EndTable();
    }

    // Delete confirmation.
    if (!m_ProjectPendingDelete.empty())
        ImGui::OpenPopup("Delete Asset");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Delete Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Delete '%s'?", m_ProjectPendingDelete.filename().string().c_str());
        ImGui::TextDisabled("You cannot undo this action.");
        if (ImGui::Button("Delete", ImVec2(120, 0))) {
            fs::remove_all(m_ProjectPendingDelete, ec);
            if (ec)
                Log::Error("Delete failed: {}", ec.message());
            if (m_ScenePath == m_ProjectPendingDelete) {
                m_ScenePath.clear();
                m_Dirty = true;
            }
            m_ProjectPendingDelete.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_ProjectPendingDelete.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (!navigateTo.empty())
        m_ProjectCurrentDir = navigateTo;
    if (!pendingOpenScene.empty() && pendingOpenScene != m_ScenePath)
        RequestSceneChange([this, pendingOpenScene] { OpenScene(pendingOpenScene); });
    ImGui::End();
}

void EditorApp::ImportFiles(const std::vector<fs::path>& files)
{
    std::error_code ec;
    fs::path target = fs::exists(m_ProjectCurrentDir, ec) ? m_ProjectCurrentDir : AssetsDir();
    for (const fs::path& file : files) {
        fs::path destination = target / file.filename();
        if (fs::is_directory(file, ec))
            fs::copy(file, destination, fs::copy_options::recursive | fs::copy_options::skip_existing, ec);
        else
            fs::copy_file(file, destination, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            Log::Error("Import of '{}' failed: {}", Platform::PathToUtf8(file), ec.message());
            continue;
        }
        std::string asset = Platform::PathToUtf8(fs::relative(destination, AssetsDir(), ec));
        GetRenderer().UnloadTexture(asset); // pick up the new file if it replaced an old one
        Log::Info("Imported {}", asset);
    }
}

std::vector<std::string> EditorApp::ListImageAssets() const
{
    std::vector<std::string> images;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(AssetsDir(), ec); it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        if (!ec && it->is_regular_file(ec) && ImageIO::IsImageFile(it->path()))
            images.push_back(Platform::PathToUtf8(fs::relative(it->path(), AssetsDir(), ec)));
    }
    std::sort(images.begin(), images.end());
    return images;
}

} // namespace ze
