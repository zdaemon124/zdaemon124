#include "EditorApp.h"

#include "EditorUI.h"

#include <IndeetsEngine/Core/Platform.h>
#include <IndeetsEngine/Scripting/ProjectAssets.h>
#include <IndeetsEngine/Scripting/ScriptEngine.h>

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <fstream>
#include <regex>

namespace ie {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

constexpr const char* kScriptTemplate = R"(using UnityEngine;

public class {NAME} : MonoBehaviour
{
    // Start is called once before the first execution of Update after the MonoBehaviour is created
    void Start()
    {

    }

    // Update is called once per frame
    void Update()
    {

    }
}
)";

float Num(const json& v, float fallback = 0.0f) { return v.is_number() ? v.get<float>() : fallback; }

std::string Str(const json& v, const char* key)
{
    auto it = v.find(key);
    return it != v.end() && it->is_string() ? it->get<std::string>() : std::string();
}

// Value a new list element starts with.
json DefaultFor(const json& meta)
{
    std::string kind = Str(meta, "kind");
    if (kind == "bool") return false;
    if (kind == "int" || kind == "float" || kind == "enum" || kind == "layermask") return 0;
    if (kind == "string") return "";
    if (kind == "vector2" || kind == "vector2int") return json::array({0, 0});
    if (kind == "vector3" || kind == "vector3int") return json::array({0, 0, 0});
    if (kind == "vector4" || kind == "rect") return json::array({0, 0, 0, 0});
    if (kind == "quaternion") return json::array({0, 0, 0, 1});
    if (kind == "color") return json::array({1, 1, 1, 1});
    if (kind == "bounds") return json::array({0, 0, 0, 0, 0, 0});
    if (kind == "list") return json::array();
    if (kind == "struct") return json::object();
    return nullptr;
}

bool DrawFloats(json& value, int count, bool integer, float speed)
{
    float v[6] = {0, 0, 0, 0, 0, 0};
    for (int i = 0; i < count; ++i)
        v[i] = value.is_array() && i < int(value.size()) ? Num(value[size_t(i)]) : 0.0f;
    bool changed = false;
    if (integer) {
        int iv[4] = {int(v[0]), int(v[1]), int(v[2]), int(v[3])};
        changed = ImGui::DragScalarN("##v", ImGuiDataType_S32, iv, count, 0.1f);
        for (int i = 0; i < count; ++i)
            v[i] = float(iv[i]);
    } else {
        changed = ImGui::DragScalarN("##v", ImGuiDataType_Float, v, count, speed, nullptr, nullptr, "%.3f");
    }
    if (changed) {
        value = json::array();
        for (int i = 0; i < count; ++i)
            value.push_back(integer ? json(int(v[i])) : json(v[i]));
    }
    return changed;
}

// Quaternions are edited as Euler angles, like Unity's inspector.
bool DrawQuaternion(json& value)
{
    glm::quat q(1.0f, 0.0f, 0.0f, 0.0f);
    if (value.is_array() && value.size() == 4)
        q = glm::quat(Num(value[3], 1.0f), Num(value[0]), Num(value[1]), Num(value[2]));
    Transform t;
    t.rotation = glm::normalize(q);
    glm::vec3 euler = t.EulerAngles();
    if (!ImGui::DragFloat3("##v", glm::value_ptr(euler), 0.5f, 0.0f, 0.0f, "%.1f"))
        return false;
    t.SetEulerAngles(euler);
    value = json::array({t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w});
    return true;
}

bool IsBuiltinType(const std::string& type)
{
    static const char* kBuiltins[] = {"Rigidbody", "Collider", "BoxCollider", "SphereCollider", "CapsuleCollider",
                                      "Renderer", "MeshRenderer", "MeshFilter", "Light", "Camera"};
    return std::find_if(std::begin(kBuiltins), std::end(kBuiltins), [&](const char* b) { return type == b; }) !=
           std::end(kBuiltins);
}

// Can an object reference field of `type` point at this entity?
bool EntityMatches(const Entity& e, const std::string& type)
{
    if (type == "GameObject" || type == "Transform" || type == "Component" || type == "Object")
        return true;
    if (type == "Rigidbody") return e.rigidbody.has_value();
    if (type == "Collider") return e.collider.has_value();
    if (type == "BoxCollider") return e.collider && e.collider->shape == ColliderShape::Box;
    if (type == "SphereCollider") return e.collider && e.collider->shape == ColliderShape::Sphere;
    if (type == "CapsuleCollider") return e.collider && e.collider->shape == ColliderShape::Capsule;
    if (type == "Renderer" || type == "MeshRenderer" || type == "MeshFilter") return e.meshRenderer.has_value();
    if (type == "Light") return e.light.has_value();
    if (type == "Camera") return e.camera.has_value();
    if (type == "MonoBehaviour" || type == "Behaviour")
        return !e.scripts.empty();
    // A script class: match by class name (with or without namespace).
    for (const ScriptComponent& s : e.scripts) {
        if (s.className == type)
            return true;
        size_t dot = s.className.rfind('.');
        if (dot != std::string::npos && s.className.substr(dot + 1) == type)
            return true;
    }
    return false;
}

struct FieldDrawer {
    Scene& scene;
    bool live;

    bool DrawObject(const json& meta, json& value)
    {
        std::string type = Str(meta, "type");
        if (!meta.value("sceneReference", false)) {
            ImGui::TextDisabled("None (%s) - asset references come with the asset importer", type.c_str());
            return false;
        }
        EntityID id = value.is_object() && value.contains("entity") ? value["entity"].get<EntityID>() : 0;
        const Entity* target = id ? scene.Get(id) : nullptr;
        std::string label = target ? target->name + " (" + type + ")" : (id ? "Missing (" + type + ")" : "None (" + type + ")");
        bool changed = false;
        float clearWidth = ImGui::GetFrameHeight();
        if (ImGui::Button(label.c_str(), ImVec2(std::max(10.0f, ImGui::GetContentRegionAvail().x - clearWidth - 4.0f), 0.0f)))
            ImGui::OpenPopup("ObjectPicker");
        UI::Tooltip("Click to choose, or drag an object from the Hierarchy here");
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("IE_ENTITY")) {
                EntityID dropped = *static_cast<const EntityID*>(payload->Data);
                if (const Entity* e = scene.Get(dropped); e && EntityMatches(*e, type)) {
                    value = json{{"entity", dropped}};
                    changed = true;
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::SameLine(0.0f, 4.0f);
        if (ImGui::Button("x", ImVec2(clearWidth, 0.0f)) && !value.is_null()) {
            value = nullptr;
            changed = true;
        }
        UI::Tooltip("Clear");
        if (ImGui::BeginPopup("ObjectPicker")) {
            static std::string filter;
            ImGui::SetNextItemWidth(220.0f);
            ImGui::InputTextWithHint("##filter", "Search...", &filter);
            if (ImGui::Selectable("None", id == 0)) {
                value = nullptr;
                changed = true;
            }
            for (const auto& e : scene.Entities()) {
                if (!EntityMatches(*e, type))
                    continue;
                if (!filter.empty() && e->name.find(filter) == std::string::npos)
                    continue;
                ImGui::PushID(int(e->id));
                if (ImGui::Selectable(e->name.c_str(), e->id == id)) {
                    value = json{{"entity", e->id}};
                    changed = true;
                }
                ImGui::PopID();
            }
            ImGui::EndPopup();
        }
        return changed;
    }

    bool DrawEnum(const json& meta, json& value)
    {
        const json& names = meta["enumNames"];
        const json& values = meta["enumValues"];
        long long current = value.is_number() ? value.get<long long>() : 0;
        bool changed = false;
        if (meta.value("flags", false)) {
            std::string preview;
            for (size_t i = 0; i < values.size(); ++i) {
                long long bit = values[i].get<long long>();
                if (bit != 0 && (current & bit) == bit)
                    preview += (preview.empty() ? "" : ", ") + names[i].get<std::string>();
            }
            if (ImGui::BeginCombo("##v", preview.empty() ? "Nothing" : preview.c_str())) {
                for (size_t i = 0; i < values.size(); ++i) {
                    long long bit = values[i].get<long long>();
                    if (bit == 0)
                        continue;
                    bool on = (current & bit) == bit;
                    if (ImGui::Checkbox(names[i].get<std::string>().c_str(), &on)) {
                        current = on ? current | bit : current & ~bit;
                        value = current;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            return changed;
        }
        std::string preview = std::to_string(current);
        for (size_t i = 0; i < values.size(); ++i)
            if (values[i].get<long long>() == current)
                preview = names[i].get<std::string>();
        if (ImGui::BeginCombo("##v", preview.c_str())) {
            for (size_t i = 0; i < values.size(); ++i) {
                long long v = values[i].get<long long>();
                if (ImGui::Selectable(names[i].get<std::string>().c_str(), v == current)) {
                    value = v;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    // One field row (or a tree of rows for lists and structs).
    bool Draw(const std::string& label, const json& meta, json& value, int depth)
    {
        std::string kind = Str(meta, "kind");
        ImGui::PushID(label.c_str());
        bool changed = false;

        if (kind == "list" || kind == "struct") {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            bool open = ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_FramePadding);
            if (meta.contains("tooltip"))
                UI::Tooltip(Str(meta, "tooltip").c_str());
            ImGui::TableNextColumn();
            if (kind == "list") {
                if (!value.is_array())
                    value = json::array();
                ImGui::TextDisabled("%zu element(s)", value.size());
            }
            if (open) {
                if (kind == "list") {
                    int size = int(value.size());
                    UI::PropertyLabel("Size");
                    if (ImGui::DragInt("##size", &size, 0.1f, 0, 100000)) {
                        size = std::max(0, size);
                        const json& element = meta["element"];
                        while (int(value.size()) < size)
                            value.push_back(value.empty() ? DefaultFor(element) : value.back());
                        while (int(value.size()) > size)
                            value.erase(value.size() - 1);
                        changed = true;
                    }
                    for (size_t i = 0; i < value.size(); ++i)
                        changed |= Draw("Element " + std::to_string(i), meta["element"], value[i], depth + 1);
                } else {
                    if (!value.is_object())
                        value = json::object();
                    for (const json& field : meta.value("fields", json::array())) {
                        std::string name = Str(field, "name");
                        json& child = value[name];
                        if (child.is_null() && field.contains("default"))
                            child = field["default"];
                        changed |= Draw(Str(field, "label"), field, child, depth + 1);
                    }
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
            return changed;
        }

        UI::PropertyLabel(label.c_str());
        if (meta.contains("tooltip") && ImGui::IsItemHovered())
            UI::Tooltip(Str(meta, "tooltip").c_str());
        if (kind == "float") {
            float v = Num(value);
            if (meta.contains("range")) {
                changed = ImGui::SliderFloat("##v", &v, Num(meta["range"][0]), Num(meta["range"][1]));
            } else {
                changed = ImGui::DragFloat("##v", &v, 0.05f);
                if (meta.contains("min"))
                    v = std::max(v, Num(meta["min"]));
            }
            if (changed)
                value = v;
        } else if (kind == "int" || kind == "layermask") {
            int v = value.is_number() ? value.get<int>() : 0;
            if (meta.contains("range"))
                changed = ImGui::SliderInt("##v", &v, int(Num(meta["range"][0])), int(Num(meta["range"][1])));
            else
                changed = ImGui::DragInt("##v", &v, 0.1f);
            if (changed)
                value = v;
        } else if (kind == "bool") {
            bool v = value.is_boolean() && value.get<bool>();
            if ((changed = ImGui::Checkbox("##v", &v)))
                value = v;
        } else if (kind == "string") {
            std::string v = value.is_string() ? value.get<std::string>() : "";
            changed = meta.value("multiline", false)
                          ? ImGui::InputTextMultiline("##v", &v, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 4.0f))
                          : ImGui::InputText("##v", &v);
            if (changed)
                value = v;
        } else if (kind == "enum") {
            changed = DrawEnum(meta, value);
        } else if (kind == "vector2") {
            changed = DrawFloats(value, 2, false, 0.05f);
        } else if (kind == "vector3") {
            changed = DrawFloats(value, 3, false, 0.05f);
        } else if (kind == "vector4" || kind == "rect") {
            changed = DrawFloats(value, 4, false, 0.05f);
        } else if (kind == "vector2int") {
            changed = DrawFloats(value, 2, true, 0.1f);
        } else if (kind == "vector3int") {
            changed = DrawFloats(value, 3, true, 0.1f);
        } else if (kind == "bounds") {
            changed = DrawFloats(value, 6, false, 0.05f);
        } else if (kind == "quaternion") {
            changed = DrawQuaternion(value);
        } else if (kind == "color") {
            glm::vec4 c(1.0f);
            for (int i = 0; i < 4; ++i)
                c[i] = value.is_array() && i < int(value.size()) ? Num(value[size_t(i)], 1.0f) : 1.0f;
            if ((changed = ImGui::ColorEdit4("##v", glm::value_ptr(c), ImGuiColorEditFlags_AlphaBar)))
                value = json::array({c.r, c.g, c.b, c.a});
        } else if (kind == "object") {
            changed = DrawObject(meta, value);
        } else {
            ImGui::TextDisabled("(not editable yet)");
        }
        ImGui::PopID();
        return changed;
    }

    // All fields of a script. Returns the names of the fields that changed this frame.
    std::vector<std::string> DrawFields(const json& fieldsMeta, json& values)
    {
        std::vector<std::string> changedNames;
        if (!values.is_object())
            values = json::object();
        for (const json& meta : fieldsMeta) {
            for (const json& header : meta.value("headers", json::array())) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Spacing();
                ImGui::TextUnformatted(header.get<std::string>().c_str());
            }
            if (meta.value("space", false)) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Dummy(ImVec2(0.0f, 4.0f));
            }
            std::string name = Str(meta, "name");
            json value = values.contains(name) ? values[name] : meta.value("default", json());
            if (Draw(Str(meta, "label"), meta, value, 0)) {
                values[name] = value;
                changedNames.push_back(name);
            }
        }
        return changedNames;
    }
};

uint64_t Hash(uint64_t h, uint64_t v) { return (h ^ v) * 1099511628211ull; }

} // namespace

// ------------------------------------------------------------------ compile & watch

void EditorApp::InitScripting()
{
    ScriptEngine& scripts = ScriptEngine::Get();
    if (!scripts.Initialize(true))
        return;
    scripts.SetApplicationInfo(AssetsDir(), m_ProjectDir.filename().string(), "DefaultCompany");
    UnityAssets();
    scripts.SetAssetProvider(MakeProjectAssets(
        AssetsDir(), [this](const std::string& assetPath) { return GetRenderer().LoadModel(assetPath); }, m_UnityAssets));
    m_ScriptSignature = ScriptSignature();
    scripts.CompileAsync(ScriptEngine::FindScriptFiles(AssetsDir()));
}

uint64_t EditorApp::ScriptSignature() const
{
    uint64_t h = 1469598103934665603ull;
    std::error_code ec;
    for (const fs::path& file : ScriptEngine::FindScriptFiles(AssetsDir())) {
        h = Hash(h, std::hash<std::string>()(file.generic_string()));
        h = Hash(h, uint64_t(fs::last_write_time(file, ec).time_since_epoch().count()));
        h = Hash(h, uint64_t(fs::file_size(file, ec)));
    }
    return h;
}

void EditorApp::UpdateScripting()
{
    ScriptEngine& scripts = ScriptEngine::Get();
    if (!scripts.IsAvailable())
        return;

    ScriptEngine::CompileState state = scripts.Poll();
    if (state == ScriptEngine::CompileState::Failed)
        m_FocusConsole = true;
    if ((state == ScriptEngine::CompileState::Succeeded || state == ScriptEngine::CompileState::Failed) &&
        m_PlayAfterCompile && !scripts.IsCompiling() && !m_ScriptsDirty) {
        m_PlayAfterCompile = false;
        Play();
    }

    // Like Unity: scripts saved in an external editor are recompiled when noticed; changes made
    // while playing are compiled after Stop.
    if (Time() - m_ScriptScanTime > 1.0) {
        m_ScriptScanTime = Time();
        uint64_t signature = ScriptSignature();
        if (signature != m_ScriptSignature) {
            m_ScriptSignature = signature;
            m_ScriptsDirty = true;
        }
    }
    if (m_ScriptsDirty && !IsPlaying() && !scripts.IsCompiling()) {
        m_ScriptsDirty = false;
        scripts.CompileAsync(ScriptEngine::FindScriptFiles(AssetsDir()));
    }
}

void EditorApp::RecompileScripts()
{
    m_ScriptSignature = ScriptSignature();
    m_ScriptsDirty = true;
}

// ------------------------------------------------------------------ script assets

fs::path EditorApp::CreateScriptAsset(const fs::path& folder, const std::string& baseName)
{
    std::error_code ec;
    fs::create_directories(folder, ec);
    std::string name = baseName;
    for (int i = 1; fs::exists(folder / (name + ".cs")); ++i)
        name = baseName + std::to_string(i);
    fs::path path = folder / (name + ".cs");
    std::string text = kScriptTemplate;
    text.replace(text.find("{NAME}"), 6, name);
    std::ofstream(path, std::ios::binary) << text;
    InvalidateProjectCache();
    RecompileScripts();
    Log::Info("Created script {}", Platform::PathToUtf8(path));
    return path;
}

// Renaming a script file renames its class too (Unity requires them to match) and updates the scene.
void EditorApp::OnScriptRenamed(const fs::path& oldPath, const fs::path& newPath)
{
    std::string oldName = oldPath.stem().string(), newName = newPath.stem().string();
    std::string text = SceneSerializer::ReadTextFile(newPath);
    std::regex classDecl("\\bclass\\s+" + oldName + "\\b");
    if (std::regex_search(text, classDecl)) {
        text = std::regex_replace(text, classDecl, "class " + newName, std::regex_constants::format_first_only);
        std::ofstream(newPath, std::ios::binary) << text;
    }
    bool changed = false;
    for (auto& e : m_Scene.Entities())
        for (ScriptComponent& s : e->scripts)
            if (s.className == oldName) {
                s.className = newName;
                changed = true;
            }
    if (changed)
        MarkDirty();
    RecompileScripts();
}

fs::path EditorApp::FindScriptFile(const std::string& className) const
{
    std::string stem = className.substr(className.rfind('.') == std::string::npos ? 0 : className.rfind('.') + 1);
    for (const fs::path& file : ScriptEngine::FindScriptFiles(AssetsDir()))
        if (file.stem() == stem)
            return file;
    return {};
}

void EditorApp::AttachScript(EntityID id, const std::string& className)
{
    Entity* e = m_Scene.Get(id);
    if (!e || className.empty())
        return;
    if (IsPlaying()) {
        ScriptEngine::Get().AddScript(id, className);
        return;
    }
    e->scripts.push_back({className, true, "{}"});
    MarkDirty();
}

void EditorApp::StartScriptsOf(EntityID root)
{
    ScriptEngine& scripts = ScriptEngine::Get();
    if (!IsPlaying() || !scripts.IsPlaying())
        return;
    for (EntityID id : m_Scene.Subtree(root))
        if (const Entity* e = m_Scene.Get(id))
            for (const ScriptComponent& s : e->scripts)
                scripts.AddScript(id, s.className, s.fields);
}

// ------------------------------------------------------------------ inspector

bool EditorApp::DrawScriptComponents(Entity& entity)
{
    ScriptEngine& scripts = ScriptEngine::Get();
    // Live view: the running instances of the scene being played (not prefab or preview scenes).
    bool live = IsPlaying() && scripts.IsPlaying() && m_Scene.Get(entity.id) == &entity;
    FieldDrawer drawer{m_Scene, live};
    bool changed = false;

    if (live) {
        json instances = scripts.EntityScripts(entity.id);
        for (size_t i = 0; i < instances.size(); ++i) {
            json& instance = instances[i];
            std::string className = Str(instance, "class");
            const json* type = scripts.FindScriptType(className);
            ImGui::PushID(int(i) + 1000);
            bool keep = true;
            ImGui::Spacing();
            std::string title = (type ? Str(*type, "displayName") : className) + " (Script)";
            bool open = ImGui::CollapsingHeader(title.c_str(), &keep, ImGuiTreeNodeFlags_DefaultOpen);
            if (open && UI::BeginProperties("script")) {
                bool enabled = instance.value("enabled", true);
                if (UI::PropertyBool("Enabled", enabled))
                    scripts.SetScriptEnabled(entity.id, int(i), enabled);
                if (type)
                    for (const std::string& name : drawer.DrawFields((*type)["fields"], instance["fields"]))
                        scripts.SetScriptField(entity.id, int(i), name, instance["fields"][name]);
                UI::EndProperties();
            }
            if (!keep)
                scripts.RemoveScript(entity.id, int(i));
            ImGui::PopID();
            if (!keep)
                break;
        }
        return false; // play mode edits are thrown away on Stop, like Unity
    }

    for (size_t i = 0; i < entity.scripts.size();) {
        ScriptComponent& s = entity.scripts[i];
        const json* type = scripts.FindScriptType(s.className);
        ImGui::PushID(int(i) + 1000);
        bool keep = true;
        ImGui::Spacing();
        std::string title = (type ? Str(*type, "displayName") : s.className) + (type ? " (Script)" : " (Missing Script)");
        bool open = ImGui::CollapsingHeader(title.c_str(), &keep, ImGuiTreeNodeFlags_DefaultOpen);
        if (open && UI::BeginProperties("script")) {
            UI::PropertyLabel("Script");
            fs::path file = FindScriptFile(s.className);
            std::string fileLabel = file.empty() ? s.className + ".cs (not found)" : file.filename().string();
            if (ImGui::Button(fileLabel.c_str(), ImVec2(-FLT_MIN, 0.0f)) && !file.empty())
                Platform::OpenWithDefaultApp(file);
            UI::Tooltip("Open in the code editor");
            changed |= UI::PropertyBool("Enabled", s.enabled);
            if (type) {
                json values = json::parse(s.fields, nullptr, false);
                if (!drawer.DrawFields((*type)["fields"], values).empty()) {
                    s.fields = values.dump();
                    changed = true;
                }
            }
            UI::EndProperties();
            if (!type) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.3f, 1.0f));
                ImGui::TextWrapped(scripts.IsCompiling()        ? "Scripts are compiling..."
                                   : scripts.HasCompileErrors() ? "The script cannot be loaded: fix the compile errors in the Console."
                                   : !scripts.IsAvailable()     ? "C# scripting is unavailable (see the Console)."
                                                                : "The class was not found. The script file name must match its class name.");
                ImGui::PopStyleColor();
            }
        }
        ImGui::PopID();
        if (!keep) {
            entity.scripts.erase(entity.scripts.begin() + std::ptrdiff_t(i));
            changed = true;
        } else {
            ++i;
        }
    }
    return changed;
}

void EditorApp::DrawAddScriptMenu(Entity& entity, bool& changed)
{
    ScriptEngine& scripts = ScriptEngine::Get();
    if (!ImGui::BeginMenu("Scripts"))
        return;
    if (!scripts.IsAvailable()) {
        ImGui::TextDisabled("C# scripting is unavailable:");
        ImGui::TextDisabled("%s", scripts.Error().c_str());
    } else if (scripts.ScriptTypes().empty()) {
        ImGui::TextDisabled(scripts.IsCompiling() ? "Compiling..." : "No scripts in the project yet");
    }
    for (const json& type : scripts.ScriptTypes()) {
        std::string name = Str(type, "name");
        if (ImGui::MenuItem(Str(type, "displayName").c_str())) {
            AttachScript(entity.id, name);
            changed = !IsPlaying();
        }
        UI::Tooltip(Str(type, "fullName").c_str());
    }
    ImGui::Separator();
    if (ImGui::MenuItem("New Script", nullptr, false, scripts.IsAvailable() && !IsPlaying())) {
        fs::path path = CreateScriptAsset(AssetsDir() / "Scripts", "NewBehaviourScript");
        AttachScript(entity.id, path.stem().string());
        changed = true;
    }
    UI::Tooltip("Creates Assets/Scripts/NewBehaviourScript.cs and adds it to this object");
    ImGui::EndMenu();
}

} // namespace ie
