// Inspector for assets selected in the Project panel: images, models, prefabs, scenes.
#include "EditorApp.h"

#include "EditorUI.h"

#include <ZEngine/Core/Platform.h>

#include <algorithm>
#include <cmath>
#include <functional>

namespace ze {

namespace fs = std::filesystem;

namespace {

std::string FormatBytes(uintmax_t bytes)
{
    if (bytes >= 1024ull * 1024ull)
        return std::format("{:.1f} MB", double(bytes) / (1024.0 * 1024.0));
    if (bytes >= 1024ull)
        return std::format("{:.1f} KB", double(bytes) / 1024.0);
    return std::format("{} B", bytes);
}

} // namespace

void EditorApp::SelectAsset(const std::string& assetPath)
{
    m_SelectedAsset = assetPath;
    m_Selected = 0;
}

void EditorApp::BuildAssetPreview(const std::string& assetPath)
{
    m_PreviewAsset = assetPath;
    m_PreviewScene.Clear();
    Entity& light = m_PreviewScene.CreateEntity("Light");
    light.light = LightComponent{};
    light.transform.SetEulerAngles({45.0f, -35.0f, 0.0f});

    fs::path file = Platform::Utf8ToPath(assetPath);
    EntityID root = 0;
    if (ModelImporter::IsModelFile(file)) {
        if (const ModelAsset* model = GetRenderer().LoadModel(assetPath))
            root = InstantiateModel(m_PreviewScene, *model, 0);
    } else if (file.extension() == ".zprefab") {
        root = SceneSerializer::InstantiatePrefab(m_PreviewScene, AssetsDir() / file, 0);
    }
    m_PreviewScene.UpdateWorldTransforms();

    // Frame the combined bounds of every mesh.
    glm::vec3 bmin(std::numeric_limits<float>::max()), bmax(-std::numeric_limits<float>::max());
    for (const auto& e : m_PreviewScene.Entities()) {
        if (!e->meshRenderer)
            continue;
        const Mesh* mesh = GetRenderer().FindMesh(e->meshRenderer->mesh);
        if (!mesh)
            continue;
        for (int c = 0; c < 8; ++c) {
            glm::vec3 corner((c & 1) ? mesh->boundsMax.x : mesh->boundsMin.x, (c & 2) ? mesh->boundsMax.y : mesh->boundsMin.y,
                             (c & 4) ? mesh->boundsMax.z : mesh->boundsMin.z);
            glm::vec3 w = e->world * glm::vec4(corner, 1.0f);
            bmin = glm::min(bmin, w);
            bmax = glm::max(bmax, w);
        }
    }
    if (bmin.x > bmax.x) {
        bmin = glm::vec3(-0.5f);
        bmax = glm::vec3(0.5f);
    }
    m_PreviewCenter = (bmin + bmax) * 0.5f;
    m_PreviewSize = bmax - bmin;
    m_PreviewDistance = std::max(glm::length(bmax - bmin), 0.1f) * 1.3f;
    (void)root;
}

void EditorApp::DrawAssetInspector()
{
    // Work on a copy: actions below (e.g. "Add to Scene") change the selection.
    const std::string asset = m_SelectedAsset;
    fs::path file = AssetsDir() / Platform::Utf8ToPath(asset);
    std::error_code ec;
    if (!fs::exists(file, ec)) {
        m_SelectedAsset.clear();
        return;
    }
    std::string ext = file.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    bool isDir = fs::is_directory(file, ec);

    ImGui::TextUnformatted(file.filename().string().c_str());
    ImGui::TextDisabled("%s", asset.c_str());
    if (!isDir)
        ImGui::TextDisabled("%s on disk", FormatBytes(fs::file_size(file, ec)).c_str());
    ImGui::Separator();

    // ---- Image
    if (ImageIO::IsImageFile(file)) {
        if (Texture* tex = GetRenderer().LoadTexture(asset)) {
            float w = ImGui::GetContentRegionAvail().x;
            float h = std::min(w * float(tex->height) / float(tex->width), 320.0f);
            float iw = h * float(tex->width) / float(tex->height);
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddRectFilled(p, {p.x + w, p.y + h}, IM_COL32(40, 40, 40, 255));
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (w - iw) * 0.5f);
            ImGui::Image(ImTextureRef(m_ImGui->Texture(*tex)), ImVec2(iw, h));
            if (UI::BeginProperties("img")) {
                UI::PropertyLabel("Size");
                ImGui::Text("%u x %u px", tex->width, tex->height);
                UI::PropertyLabel("Mip levels");
                ImGui::Text("%u", tex->mipLevels);
                UI::PropertyLabel("GPU memory");
                ImGui::Text("%s", FormatBytes(uintmax_t(tex->width) * tex->height * 4 * 4 / 3).c_str());
                UI::EndProperties();
            }
        }
        if (ImGui::Button("Reimport"))
            GetRenderer().UnloadTexture(asset);
        ImGui::SameLine();
        if (ImGui::Button("Create UI Image")) {
            std::string asset = asset;
            CreateUIImage(asset, {0.0f, 0.0f}, 0);
            m_FocusUIPanel = true;
        }
        return;
    }

    // ---- Scene
    if (ext == ".zscene") {
        if (ImGui::Button("Open Scene", ImVec2(-FLT_MIN, 0.0f))) {
            fs::path path = file;
            RequestSceneChange([this, path] { OpenScene(path); });
        }
        return;
    }

    bool isModel = ModelImporter::IsModelFile(file);
    bool isPrefab = ext == ".zprefab";
    if (!isModel && !isPrefab) {
        if (isDir)
            ImGui::TextDisabled("Folder");
        return;
    }

    // ---- Preview (models and prefabs), drag to orbit.
    m_PreviewVisible = true;
    if (m_PreviewAsset != asset)
        BuildAssetPreview(asset);
    float pw = ImGui::GetContentRegionAvail().x;
    ImVec2 previewSize(pw, std::min(pw * 0.7f, 300.0f));
    ImGui::Image(ImTextureRef(m_ImGui->Texture(*m_PreviewTarget)), previewSize);
    if (ImGui::IsItemHovered()) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            m_PreviewYaw += ImGui::GetIO().MouseDelta.x * 0.4f;
            m_PreviewPitch = std::clamp(m_PreviewPitch + ImGui::GetIO().MouseDelta.y * 0.4f, -85.0f, 85.0f);
        }
        if (ImGui::GetIO().MouseWheel != 0.0f)
            m_PreviewDistance *= ImGui::GetIO().MouseWheel > 0 ? 0.9f : 1.1f;
        UI::Tooltip("Drag to rotate, wheel to zoom");
    }
    if (ImGui::Button("Add to Scene")) {
        InstantiateAsset(asset, 0);
        return;
    }
    UI::Tooltip("Or drag the asset from the Project panel into the Scene view / Hierarchy");

    // ---- Model details
    if (isModel) {
        const ModelAsset* model = GetRenderer().LoadModel(asset);
        ImGui::SameLine();
        if (ImGui::Button("Reimport")) {
            GetRenderer().UnloadModel(asset);
            m_PreviewAsset.clear();
            return;
        }
        if (!model) {
            ImGui::TextColored({1, 0.4f, 0.35f, 1}, "Import failed, see the Console.");
            return;
        }
        if (UI::BeginProperties("model")) {
            UI::PropertyLabel("Format");
            ImGui::TextUnformatted(model->format.c_str());
            UI::PropertyLabel("Nodes");
            ImGui::Text("%zu", model->nodes.size());
            UI::PropertyLabel("Meshes");
            ImGui::Text("%zu", model->parts.size());
            UI::PropertyLabel("Triangles");
            ImGui::Text("%u", model->totalTriangles);
            UI::PropertyLabel("Vertices");
            ImGui::Text("%u", model->totalVertices);
            glm::vec3 size = m_PreviewSize; // with node transforms applied
            UI::PropertyLabel("Size (m)");
            ImGui::Text("%.2f x %.2f x %.2f", size.x, size.y, size.z);
            UI::EndProperties();
        }

        if (ImGui::CollapsingHeader(std::format("Hierarchy ({} nodes)", model->nodes.size()).c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            std::function<void(int)> drawNode = [&](int index) {
                const ModelAsset::Node& node = model->nodes[size_t(index)];
                bool hasChildren = false;
                for (const auto& n : model->nodes)
                    if (n.parent == index) { hasChildren = true; break; }
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
                if (!hasChildren && node.parts.empty())
                    flags |= ImGuiTreeNodeFlags_Leaf;
                bool open = ImGui::TreeNodeEx((void*)(intptr_t)index, flags, "%s", node.name.c_str());
                if (open) {
                    for (int part : node.parts) {
                        const ModelAsset::Part& p = model->parts[size_t(part)];
                        std::string mat = p.material >= 0 ? model->materials[size_t(p.material)].name : "no material";
                        ImGui::TreeNodeEx((void*)(intptr_t)(100000 + part), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen,
                                          "Mesh: %s  (%u tris, %s)%s", p.name.c_str(), p.triangleCount, mat.c_str(),
                                          p.skinned ? "  [skinned]" : "");
                    }
                    for (int i = 0; i < int(model->nodes.size()); ++i)
                        if (model->nodes[size_t(i)].parent == index)
                            drawNode(i);
                    ImGui::TreePop();
                }
            };
            for (int i = 0; i < int(model->nodes.size()); ++i)
                if (model->nodes[size_t(i)].parent < 0)
                    drawNode(i);
        }
        if (ImGui::CollapsingHeader(std::format("Materials ({})", model->materials.size()).c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            for (const auto& m : model->materials) {
                ImGui::ColorButton(("##" + m.name).c_str(), ImVec4(m.baseColor.r, m.baseColor.g, m.baseColor.b, 1.0f),
                                   ImGuiColorEditFlags_NoTooltip, ImVec2(18, 18));
                ImGui::SameLine();
                if (!m.baseColorTexture.empty())
                    if (Texture* tex = GetRenderer().LoadTexture(m.baseColorTexture)) {
                        ImGui::Image(ImTextureRef(m_ImGui->Texture(*tex)), ImVec2(18, 18));
                        ImGui::SameLine();
                    }
                ImGui::TextUnformatted(m.name.c_str());
                if (!m.baseColorTexture.empty()) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", m.baseColorTexture.c_str());
                }
            }
        }
        if (ImGui::CollapsingHeader(std::format("Animations ({})", model->animations.size()).c_str())) {
            if (model->animations.empty())
                ImGui::TextDisabled("No animations");
            for (const auto& a : model->animations)
                ImGui::BulletText("%s  (%.2f s)", a.name.c_str(), a.duration);
            ImGui::TextDisabled("Animation playback is coming with the skinned mesh update.");
        }
        return;
    }

    // ---- Prefab: edit its objects directly; changes are saved and applied to all instances.
    if (m_PrefabScenePath != asset) {
        m_PrefabScene.Clear();
        m_PrefabSelected = SceneSerializer::InstantiatePrefab(m_PrefabScene, file, 0);
        m_PrefabScenePath = asset;
        m_PrefabDirty = false;
    }
    EntityID prefabRoot = m_PrefabScene.Entities().empty() ? 0 : m_PrefabScene.Entities().front()->id;
    ImGui::SameLine();
    int instances = 0;
    for (const auto& e : m_Scene.Entities())
        instances += e->prefab == asset;
    ImGui::TextDisabled("%d instance(s) in this scene", instances);

    if (ImGui::CollapsingHeader("Prefab Contents", ImGuiTreeNodeFlags_DefaultOpen)) {
        std::function<void(EntityID)> drawNode = [&](EntityID id) {
            Entity* e = m_PrefabScene.Get(id);
            std::vector<EntityID> children = m_PrefabScene.Children(id);
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen |
                                       ImGuiTreeNodeFlags_OpenOnArrow;
            if (children.empty())
                flags |= ImGuiTreeNodeFlags_Leaf;
            if (id == m_PrefabSelected)
                flags |= ImGuiTreeNodeFlags_Selected;
            bool open = ImGui::TreeNodeEx((void*)(intptr_t)id, flags, "%s", e->name.c_str());
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                m_PrefabSelected = id;
            if (open) {
                for (EntityID c : children)
                    drawNode(c);
                ImGui::TreePop();
            }
        };
        if (prefabRoot)
            drawNode(prefabRoot);
    }
    if (Entity* e = m_PrefabScene.Get(m_PrefabSelected)) {
        ImGui::Separator();
        if (DrawEntityInspector(*e))
            m_PrefabDirty = true;
        if (m_PrefabDirty)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.45f, 0.75f, 1.0f));
        bool save = ImGui::Button(m_PrefabDirty ? "Save Prefab *" : "Save Prefab", ImVec2(-FLT_MIN, 0.0f));
        if (m_PrefabDirty)
            ImGui::PopStyleColor();
        if (save && prefabRoot) {
            m_PrefabDirty = false;
            m_PrefabScene.UpdateWorldTransforms();
            SceneSerializer::SavePrefab(m_PrefabScene, prefabRoot, file);
            RefreshPrefabInstances(asset);
            m_PreviewAsset.clear();
            Log::Info("Saved prefab {}", asset);
        }
        UI::Tooltip("Writes the prefab and updates every instance in the open scene");
    }
}

} // namespace ze
