// Unity project import test.
//   UnityImportTests <test project dir>                 checks against the synthetic project (CTest):
//                                                       import, then (with .NET) the project's scripts
//                                                       running on it: prefabs, ScriptableObjects, Resources
//   UnityImportTests --convert <Assets dir> <scene or prefab> [out.zscene]
//                                                       converts a real Unity asset and prints the report
#include <IndeetsEngine/Assets/Model.h>
#include <IndeetsEngine/Assets/UnityImporter.h>
#include <IndeetsEngine/Core/Log.h>
#include <IndeetsEngine/Core/Platform.h>
#include <IndeetsEngine/Physics/PhysicsWorld.h>
#include <IndeetsEngine/Scene/SceneSerializer.h>
#include <IndeetsEngine/Scripting/ProjectAssets.h>
#include <IndeetsEngine/Scripting/ScriptEngine.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <thread>

using namespace ie;
namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

int g_Failures = 0;

void Expect(bool condition, const std::string& what)
{
    std::printf("  %s  %s\n", condition ? "ok  " : "FAIL", what.c_str());
    if (!condition)
        ++g_Failures;
}

// CPU-only model loading (no renderer in tests).
struct ModelCache {
    fs::path assets;
    std::map<std::string, std::unique_ptr<ModelAsset>> models;

    const ModelAsset* Load(const std::string& path)
    {
        auto it = models.find(path);
        if (it == models.end()) {
            std::string error;
            auto model = ModelImporter::Load(assets, path, error);
            if (!model)
                Log::Warn("Model '{}' failed: {}", path, error);
            it = models.emplace(path, std::move(model)).first;
        }
        return it->second.get();
    }
};

Entity* Named(Scene& scene, const std::string& name)
{
    for (const auto& e : scene.Entities())
        if (e->name == name)
            return e.get();
    return nullptr;
}

json Fields(const Entity* e, size_t index = 0)
{
    if (!e || e->scripts.size() <= index)
        return json();
    return json::parse(e->scripts[index].fields, nullptr, false);
}

bool Near(glm::vec3 a, glm::vec3 b) { return glm::length(a - b) < 1e-4f; }

void CheckImport(const fs::path& assets)
{
    std::printf("Import\n");
    UnityAssetDatabase db(assets);
    Expect(db.PathForGuid("b0000000000000000000000000000001") == "Resources/Enemy.prefab", "GUID -> path from .meta");
    const auto& resources = db.Resources();
    Expect(resources.count("Configs/Goblin") == 1 && resources.count("Enemy") == 1 && resources.count("Data/names") == 1,
           "Resources folder index");

    Scene scene;
    UnityImportReport report;
    Expect(UnityImporter::ImportScene(db, "Scenes/Test.unity", scene, {}, &report), "scene imported");
    Expect(scene.Entities().size() == 4, "entity count (" + std::to_string(scene.Entities().size()) + ")");
    Expect(report.prefabInstances == 1 && report.scripts == 2 && report.missingScripts == 0, "report: " + report.Summary());

    std::vector<std::string> roots;
    for (const auto& e : scene.Entities())
        if (!e->parent)
            roots.push_back(e->name);
    Expect(roots == std::vector<std::string>{"Setup", "Ground", "Placed Enemy"}, "root order from SceneRoots");

    Entity* setup = Named(scene, "Setup");
    Entity* ground = Named(scene, "Ground");
    Entity* placed = Named(scene, "Placed Enemy");
    Entity* weapon = Named(scene, "Weapon");
    Expect(setup && setup->tag == "GameController" && setup->layer == 5, "tag and layer");
    Expect(ground && ground->meshRenderer && ground->meshRenderer->mesh == "Cube" && ground->collider &&
               ground->collider->shape == ColliderShape::Box && Near(ground->transform.scale, {10, 1, 10}),
           "built-in mesh, box collider, transform");

    // Prefab instance: overrides applied on top of the prefab.
    Expect(placed && Near(placed->transform.position, {5, 0, 2}), "prefab instance: transform override");
    Expect(placed && placed->meshRenderer && placed->meshRenderer->mesh == "Capsule" && placed->collider &&
               placed->collider->shape == ColliderShape::Capsule,
           "prefab instance: components from the prefab");
    Expect(weapon && placed && weapon->parent == placed->id && Near(weapon->transform.position, {0.5f, 1, 0}),
           "prefab instance: child hierarchy");
    json enemy = Fields(placed);
    Expect(placed && placed->scripts.size() == 1 && placed->scripts[0].className == "Enemy", "prefab instance: script");
    Expect(enemy.value("health", json()) == "50" && enemy.value("angry", json()) == "1" && enemy.value("title", json()) == "Grunt",
           "prefab instance: field overrides and prefab values");
    Expect(weapon && enemy["weapon"] == json{{"entity", weapon->id}}, "reference inside the prefab -> entity");
    Expect(enemy["config"].value("asset", "") == "Resources/Configs/Goblin.asset", "reference to a ScriptableObject asset");

    json setupFields = Fields(setup);
    Expect(placed && setupFields["placed"] == json{{"entity", placed->id}}, "scene reference to a prefab instance's component");
    Expect(setupFields["enemyPrefab"].value("asset", "") == "Resources/Enemy.prefab", "reference to a prefab asset");
    Expect(setupFields["spawnPoint"] == json::array({1.0, 2.0, 3.0}), "Vector3 field");

    // Prefab on its own.
    Scene prefabScene;
    EntityID root = UnityImporter::InstantiatePrefab(db, "Resources/Enemy.prefab", prefabScene, 0, {});
    Expect(root && prefabScene.Get(root)->name == "Enemy" && prefabScene.Entities().size() == 2, "prefab instantiated");

    // ScriptableObject asset.
    json goblin = UnityImporter::LoadScriptAsset(db, "Resources/Configs/Goblin.asset");
    Expect(goblin.value("class", "") == "EnemyConfig" && goblin.value("name", "") == "Goblin", "ScriptableObject class and name");
    Expect(goblin["fields"]["drops"] == json::array({"1", "4", "7"}) &&
               goblin["fields"]["tint"] == json::array({1.0, 0.5, 0.25, 1.0}) &&
               goblin["fields"]["upgrade"].value("asset", "") == "Resources/Configs/Orc.asset",
           "ScriptableObject fields");

    // Asset provider used by scripts.
    ScriptAssetProvider assetsProvider = MakeProjectAssets(assets, nullptr);
    Expect(assetsProvider.findResources("Configs", true).size() == 2 && assetsProvider.findResources("Configs/Orc", false).size() == 1,
           "Resources lookup (file and folder)");
    Expect(assetsProvider.describe("Resources/Enemy.prefab").value("kind", "") == "prefab" &&
               assetsProvider.describe("Resources/Configs/Orc.asset").value("kind", "") == "script" &&
               assetsProvider.describe("Resources/Data/names.txt").value("kind", "") == "text",
           "asset kinds");
}

ScriptEngine::CompileState CompileAndWait(ScriptEngine& engine, const std::vector<fs::path>& files)
{
    if (!engine.CompileAsync(files))
        return ScriptEngine::CompileState::Failed;
    for (int i = 0; i < 6000; ++i) {
        ScriptEngine::CompileState state = engine.Poll();
        if (state != ScriptEngine::CompileState::Compiling)
            return state;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return ScriptEngine::CompileState::Failed;
}

// The project's scripts running on the imported scene (GameSetup.cs reports "CHECK <name> ok").
void CheckRuntime(const fs::path& assets)
{
    std::printf("Runtime\n");
    ScriptEngine& scripts = ScriptEngine::Get();
    if (!scripts.Initialize(false)) {
        std::printf("  skipped: scripting unavailable (%s)\n", scripts.Error().c_str());
        return;
    }
    scripts.SetApplicationInfo(assets, "UnityImportTests", "IndeetsEngine");
    std::vector<fs::path> files = ScriptEngine::FindScriptFiles(assets);
    Expect(CompileAndWait(scripts, files) == ScriptEngine::CompileState::Succeeded, "project scripts compile");
    if (scripts.HasCompileErrors())
        return;

    auto db = std::make_shared<UnityAssetDatabase>(assets);
    scripts.SetAssetProvider(MakeProjectAssets(assets, nullptr, db));
    Scene scene;
    Expect(UnityImporter::ImportScene(*db, "Scenes/Test.unity", scene, {}), "scene imported");
    size_t before = scene.Entities().size();

    PhysicsWorld physics;
    physics.Start(scene);
    Expect(scripts.BeginPlay(scene, &physics), "BeginPlay");
    for (int frame = 0; frame < 5; ++frame) {
        physics.Update(
            scene, 1.0f / 60.0f, [&] { scripts.FixedUpdate(PhysicsWorld::kFixedTimeStep); },
            [&](const std::vector<ContactEvent>& contacts) { scripts.DispatchContacts(contacts); });
        scripts.Update(1.0f / 60.0f);
        scripts.LateUpdate();
    }
    Entity* clone = Named(scene, "Enemy(Clone)");
    Expect(clone && clone->collider && scene.IsActiveInHierarchy(clone->id), "prefab clone is in the scene");
    Expect(clone && physics.HasBody(clone->id), "prefab clone has a physics body");
    scripts.EndPlay();
    physics.Stop();
    Expect(!Named(scene, "[Prefab Assets]") && !Named(scene, "Enemy"), "prefab assets unloaded after play");
    Expect(scene.Entities().size() == before + 2, "only the clone (and its child) remain");

    std::set<std::string> expected, passed;
    std::vector<std::string> failures;
    std::regex check(R"(Check\("([A-Za-z0-9_.]+)\")");
    for (const fs::path& file : files) {
        std::ifstream in(file);
        std::stringstream text;
        text << in.rdbuf();
        std::string source = text.str();
        for (std::sregex_iterator it(source.begin(), source.end(), check), end; it != end; ++it)
            expected.insert((*it)[1]);
    }
    std::regex ok(R"(^CHECK ([A-Za-z0-9_.]+) ok$)");
    int errors = 0;
    Log::ForEachEntry([&](const LogEntry& entry) {
        std::smatch m;
        if (std::regex_match(entry.message, m, ok))
            passed.insert(m[1]);
        else if (entry.level == LogLevel::Error)
            ++errors, failures.push_back(entry.message);
    });
    for (const std::string& f : failures)
        Expect(false, f);
    for (const std::string& name : expected)
        Expect(passed.contains(name), "script check " + name);
    Expect(errors == 0, "no errors logged");
}

int Convert(const fs::path& assets, const std::string& asset, const std::string& out)
{
    UnityAssetDatabase db(assets);
    if (fs::path(asset).extension() == ".asset") {
        json so = UnityImporter::LoadScriptAsset(db, asset);
        std::printf("%s\n", so.dump(2).c_str());
        return so.is_null() ? 1 : 0;
    }
    ModelCache cache{assets, {}};
    UnityImportOptions options;
    options.loadModel = [&](const std::string& p) { return cache.Load(p); };
    UnityImportReport report;
    Scene scene;
    auto t0 = std::chrono::steady_clock::now();
    bool ok = true;
    if (UnityImporter::IsUnityScene(asset))
        ok = UnityImporter::ImportScene(db, asset, scene, options, &report);
    else
        ok = UnityImporter::InstantiatePrefab(db, asset, scene, 0, options, &report) != 0;
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("%s: %s in %.2f s, %zu entities\n%s\n", asset.c_str(), ok ? "imported" : "FAILED", seconds,
                scene.Entities().size(), report.Summary().c_str());
    std::map<std::string, int> warnings;
    for (const auto& w : report.warnings)
        ++warnings[w.substr(0, 90)];
    for (const auto& [w, n] : warnings)
        std::printf("  warning x%d: %s\n", n, w.c_str());
    if (!out.empty())
        SceneSerializer::Save(scene, out);
    return ok ? 0 : 1;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc >= 4 && std::string(argv[1]) == "--convert")
        return Convert(argv[2], argv[3], argc > 4 ? argv[4] : "");
    if (argc < 2) {
        std::printf("usage: UnityImportTests <project dir> | --convert <Assets dir> <asset> [out]\n");
        return 1;
    }
    fs::path assets = fs::path(argv[1]) / "Assets";
    CheckImport(assets);
    CheckRuntime(assets);
    std::printf("\n%s (%d failure(s))\n", g_Failures ? "FAILED" : "PASSED", g_Failures);
    return g_Failures ? 1 : 0;
}
