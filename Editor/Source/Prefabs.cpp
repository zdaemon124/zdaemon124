// Prefabs, asset instantiation and undo/redo.
#include "EditorApp.h"

#include <IndeetsEngine/Core/Platform.h>

#include <ImGuizmo.h>

namespace ie {

namespace fs = std::filesystem;

// ------------------------------------------------------------------ instantiation

EntityID EditorApp::InstantiateAsset(const std::string& assetPath, EntityID parent, const glm::vec3* worldPosition)
{
    fs::path file = Platform::Utf8ToPath(assetPath);
    EntityID root = 0;
    if (file.extension() == ".cs") {
        // A script dropped on an object is added to it, like Unity.
        if (parent)
            AttachScript(parent, file.stem().string());
        else
            Log::Warn("Drop the script on an object to add it");
        return parent;
    }
    if (ModelImporter::IsModelFile(file)) {
        if (const ModelAsset* model = GetRenderer().LoadModel(assetPath))
            root = InstantiateModel(m_Scene, *model, parent);
    } else if (UnityImporter::IsUnityPrefab(file)) {
        UnityImportReport report;
        root = UnityImporter::InstantiatePrefab(UnityAssets(), assetPath, m_Scene, parent, UnityOptions(), &report);
        if (!report.skipped.empty() || report.missingScripts)
            Log::Info("{}: {}", assetPath, report.Summary());
    } else if (file.extension() == ".zprefab") {
        root = SceneSerializer::InstantiatePrefab(m_Scene, AssetsDir() / file, parent);
        if (Entity* e = m_Scene.Get(root))
            e->prefab = assetPath;
    }
    Entity* e = m_Scene.Get(root);
    if (!e)
        return 0;

    glm::vec3 position = worldPosition ? *worldPosition
                                       : m_EditorCamera.transform.position + m_EditorCamera.transform.Forward() * 8.0f;
    if (!parent || worldPosition) {
        m_Scene.UpdateWorldTransforms();
        glm::mat4 world = e->world;
        world[3] = glm::vec4(position, 1.0f);
        m_Scene.SetWorldMatrix(*e, world);
    }
    m_Scene.UpdateWorldTransforms();
    if (IsPlaying()) {
        for (EntityID id : m_Scene.Subtree(root))
            m_Physics.AddEntity(*m_Scene.Get(id));
        StartScriptsOf(root);
    }
    Select(root);
    MarkDirty();
    return root;
}

void EditorApp::CreatePrefab(EntityID root, const fs::path& folder)
{
    Entity* e = m_Scene.Get(root);
    if (!e)
        return;
    std::string stem = e->name.empty() ? "Prefab" : e->name;
    for (char& c : stem)
        if (std::string_view("\\/:*?\"<>|").find(c) != std::string_view::npos)
            c = '_';
    fs::path path = folder / (stem + ".zprefab");
    std::error_code ec;
    for (int i = 1; fs::exists(path, ec); ++i)
        path = folder / (stem + " " + std::to_string(i) + ".zprefab");
    if (!SceneSerializer::SavePrefab(m_Scene, root, path))
        return;
    e->prefab = Platform::PathToUtf8(fs::relative(path, AssetsDir(), ec));
    InvalidateProjectCache();
    MarkDirty();
    Log::Info("Created prefab {}", e->prefab);
}

void EditorApp::RefreshPrefabInstances(const std::string& prefab, EntityID skip)
{
    std::vector<EntityID> instances;
    for (const auto& e : m_Scene.Entities())
        if (e->prefab == prefab && e->id != skip)
            instances.push_back(e->id);
    if (instances.empty())
        return;
    std::string text = SceneSerializer::ReadTextFile(AssetsDir() / Platform::Utf8ToPath(prefab));
    if (text.empty())
        return;
    for (EntityID id : instances) {
        Entity* old = m_Scene.Get(id);
        if (!old)
            continue;
        // The instance keeps its place, name and placement; the rest comes from the prefab.
        EntityID parent = old->parent;
        Transform transform = old->transform;
        std::string name = old->name;
        bool active = old->active;
        std::optional<RectTransform> rect = old->rectTransform;
        int index = m_Scene.IndexOf(id);
        bool wasSelected = m_Selected == id;
        m_Scene.DestroyEntity(id);
        EntityID root = SceneSerializer::InstantiateFromString(m_Scene, text, parent);
        Entity* e = m_Scene.Get(root);
        if (!e)
            continue;
        e->prefab = prefab;
        e->transform = transform;
        e->name = name;
        e->active = active;
        if (rect && e->rectTransform) {
            e->rectTransform->parent = rect->parent;
            e->rectTransform->anchorMin = rect->anchorMin;
            e->rectTransform->anchorMax = rect->anchorMax;
            e->rectTransform->pivot = rect->pivot;
            e->rectTransform->position = rect->position;
        }
        for (EntityID child : m_Scene.Subtree(root))
            m_Scene.Move(child, index++);
        if (wasSelected)
            m_Selected = root;
    }
    m_Scene.UpdateWorldTransforms();
    MarkDirty();
}

void EditorApp::ApplyPrefab(EntityID instanceRoot)
{
    Entity* e = m_Scene.Get(instanceRoot);
    if (!e || e->prefab.empty())
        return;
    std::string prefab = e->prefab;
    if (!SceneSerializer::SavePrefab(m_Scene, instanceRoot, AssetsDir() / Platform::Utf8ToPath(prefab)))
        return;
    RefreshPrefabInstances(prefab, instanceRoot);
    if (m_PrefabScenePath == prefab)
        m_PrefabScenePath.clear(); // reload in the asset inspector
    m_PreviewAsset.clear();
    Log::Info("Applied changes to prefab {}", prefab);
}

void EditorApp::RevertPrefab(EntityID instanceRoot)
{
    Entity* e = m_Scene.Get(instanceRoot);
    if (!e || e->prefab.empty())
        return;
    std::string prefab = e->prefab;
    // Refresh only this instance: temporarily mark the others as someone else's.
    std::vector<EntityID> others;
    for (const auto& other : m_Scene.Entities())
        if (other->prefab == prefab && other->id != instanceRoot)
            others.push_back(other->id);
    for (EntityID id : others)
        m_Scene.Get(id)->prefab = "\x01"; // placeholder, restored below
    RefreshPrefabInstances(prefab);
    for (EntityID id : others)
        if (Entity* o = m_Scene.Get(id))
            o->prefab = prefab;
}

// ------------------------------------------------------------------ undo / redo

void EditorApp::ResetUndo()
{
    m_UndoStack.clear();
    m_RedoStack.clear();
    m_UndoBaseline = SceneSerializer::ToString(m_Scene);
    m_UndoPending = false;
}

void EditorApp::CommitUndoIfIdle()
{
    // One undo step per finished gesture (drag, typing, gizmo move...), not per frame.
    if (!m_UndoPending || IsPlaying())
        return;
    if (ImGui::IsAnyItemActive() || ImGuizmo::IsUsing() || ImGui::IsMouseDown(ImGuiMouseButton_Left))
        return;
    std::string current = SceneSerializer::ToString(m_Scene);
    m_UndoPending = false;
    if (current == m_UndoBaseline)
        return;
    m_UndoStack.push_back(std::move(m_UndoBaseline));
    if (m_UndoStack.size() > 200)
        m_UndoStack.erase(m_UndoStack.begin());
    m_UndoBaseline = std::move(current);
    m_RedoStack.clear();
}

void EditorApp::Undo()
{
    if (IsPlaying() || m_UndoStack.empty())
        return;
    m_RedoStack.push_back(std::move(m_UndoBaseline));
    m_UndoBaseline = std::move(m_UndoStack.back());
    m_UndoStack.pop_back();
    SceneSerializer::FromString(m_Scene, m_UndoBaseline);
    m_UndoPending = false;
    m_Dirty = true;
    if (!m_Scene.Get(m_Selected))
        m_Selected = 0;
}

void EditorApp::Redo()
{
    if (IsPlaying() || m_RedoStack.empty())
        return;
    m_UndoStack.push_back(std::move(m_UndoBaseline));
    m_UndoBaseline = std::move(m_RedoStack.back());
    m_RedoStack.pop_back();
    SceneSerializer::FromString(m_Scene, m_UndoBaseline);
    m_UndoPending = false;
    m_Dirty = true;
    if (!m_Scene.Get(m_Selected))
        m_Selected = 0;
}

} // namespace ie
