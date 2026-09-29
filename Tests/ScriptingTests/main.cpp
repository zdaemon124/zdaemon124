// Headless test of C# scripting: compiles Tests/ScriptingTests/Scripts with the in-process compiler,
// runs a scene through the Unity lifecycle (with physics) and checks the results the scripts report
// ("CHECK <name> ok" / "FAIL <name>") plus the engine-side effects. Needs no window or GPU.
#include <IndeetsEngine/Core/Log.h>
#include <IndeetsEngine/Core/Platform.h>
#include <IndeetsEngine/Physics/PhysicsWorld.h>
#include <IndeetsEngine/Scene/Scene.h>
#include <IndeetsEngine/Scripting/ScriptEngine.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <thread>

using namespace ie;
namespace fs = std::filesystem;

namespace {

int g_Failures = 0;

void Expect(bool condition, const std::string& what)
{
    if (condition) {
        std::printf("  ok    %s\n", what.c_str());
    } else {
        std::printf("  FAIL  %s\n", what.c_str());
        ++g_Failures;
    }
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

// Every Check("name", ...) in the test scripts must have reported "CHECK name ok".
std::set<std::string> ExpectedChecks(const fs::path& dir)
{
    std::set<std::string> names;
    std::regex check(R"(Check\("([A-Za-z0-9_.]+)\")");
    for (const auto& file : ScriptEngine::FindScriptFiles(dir)) {
        std::ifstream in(file);
        std::stringstream text;
        text << in.rdbuf();
        std::string s = text.str();
        for (std::sregex_iterator it(s.begin(), s.end(), check), end; it != end; ++it)
            names.insert((*it)[1]);
    }
    return names;
}

} // namespace

int main(int argc, char** argv)
{
    fs::path scriptsDir = argc > 1 ? fs::path(argv[1]) : Platform::ExecutableDir() / "TestScripts";
    std::printf("Scripts: %s\n", Platform::PathToUtf8(scriptsDir).c_str());

    ScriptEngine& scripts = ScriptEngine::Get();
    if (!scripts.Initialize(false)) {
        std::printf("FAIL: scripting unavailable: %s\n", scripts.Error().c_str());
        return 1;
    }

    // ---- Compile errors are reported with file and line, and block nothing else.
    fs::path broken = fs::temp_directory_path() / "IndeetsEngineBrokenScript.cs";
    {
        std::ofstream out(broken);
        out << "using UnityEngine;\npublic class Broken : MonoBehaviour {\n    void Update() { undefinedName++; }\n}\n";
    }
    Expect(CompileAndWait(scripts, {broken}) == ScriptEngine::CompileState::Failed, "compile error detected");
    Expect(!scripts.Diagnostics().empty() && scripts.Diagnostics()[0].error && scripts.Diagnostics()[0].line == 3 &&
               scripts.Diagnostics()[0].id == "CS0103",
           "compile error has file/line/id");
    fs::remove(broken);

    // ---- Real scripts
    std::vector<fs::path> files = ScriptEngine::FindScriptFiles(scriptsDir);
    Expect(files.size() >= 5, "test scripts found");
    Expect(CompileAndWait(scripts, files) == ScriptEngine::CompileState::Succeeded, "test scripts compile");
    uint64_t version = scripts.Version();
    if (scripts.HasCompileErrors())
        return 1;

    const nlohmann::json* lifecycleType = scripts.FindScriptType("Lifecycle");
    Expect(lifecycleType != nullptr, "script type listed");
    if (lifecycleType) {
        bool speed = false, hidden = false, nonSerialized = false, label = false;
        for (const auto& f : (*lifecycleType)["fields"]) {
            std::string name = f.value("name", std::string());
            speed |= name == "speed" && f.value("kind", std::string()) == "float" && f["default"] == 1.0;
            hidden |= name == "hidden" && f.value("label", std::string()) == "Hidden";
            label |= name == "label" && f["default"] == "default";
            nonSerialized |= name == "notSerialized";
        }
        Expect(speed && hidden && label && !nonSerialized, "inspector field metadata (defaults, private [SerializeField], [NonSerialized])");
    }

    // ---- Scene
    Scene scene;
    Entity& ground = scene.CreatePrimitive(PrimitiveType::Plane, "Ground");
    (void)ground;
    EntityID moverId = scene.CreateEntity("Mover").id;
    scene.Get(moverId)->scripts.push_back({"Mover", true, "{}"});

    EntityID templateId = scene.CreateEntity("Template").id;
    EntityID childId = scene.CreateEntity("Child").id;
    scene.Get(childId)->parent = templateId;
    scene.Get(templateId)->scripts.push_back(
        {"Counter", true, nlohmann::json{{"step", 4}, {"child", {{"entity", childId}}}}.dump()});

    EntityID lifecycleId = scene.CreateEntity("Lifecycle").id;
    nlohmann::json lifecycleFields = {
        {"speed", 5.0}, {"hidden", 7}, {"offset", {1, 2, 3}}, {"tint", {0.5, 0.5, 0.5, 1.0}},
        {"target", {{"entity", moverId}}}, {"mover", {{"entity", moverId}}}, {"label", "from scene"},
        {"numbers", {10, 20, 30}}, {"notSerialized", 99}};
    scene.Get(lifecycleId)->scripts.push_back({"Lifecycle", true, lifecycleFields.dump()});

    EntityID spawnerId = scene.CreateEntity("Spawner").id;
    scene.Get(spawnerId)->scripts.push_back(
        {"Spawner", true, nlohmann::json{{"template", {{"entity", templateId}}}}.dump()});

    Entity& ball = scene.CreatePrimitive(PrimitiveType::Sphere, "Ball");
    ball.transform.position = {0.0f, 5.0f, 0.0f};
    ball.rigidbody = RigidbodyComponent{};
    ball.scripts.push_back({"PhysicsProbe", true, "{}"});

    scene.CreateEntity("Math").scripts.push_back({"MathChecks", true, "{}"});
    scene.CreateEntity("Missing").scripts.push_back({"NoSuchScript", true, "{}"});
    scene.UpdateWorldTransforms();

    // ---- Play
    PhysicsWorld physics;
    physics.Start(scene);
    Expect(scripts.BeginPlay(scene, &physics), "BeginPlay");
    const float dt = 1.0f / 60.0f;
    for (int frame = 1; frame <= 90; ++frame) {
        physics.Update(scene, dt * scripts.TimeScale(), [&] { scripts.FixedUpdate(PhysicsWorld::kFixedTimeStep); });
        scripts.Update(dt);
        scripts.LateUpdate();
    }

    Entity* mover = scene.Get(moverId);
    Expect(mover && std::abs(mover->transform.position.x - 1.5f) < 0.05f,
           "script moved the entity (x = " + std::to_string(mover ? mover->transform.position.x : 0.0f) + ")");

    // Live inspector access while playing.
    nlohmann::json live = scripts.EntityScripts(lifecycleId);
    Expect(live.size() == 1 && live[0]["class"] == "Lifecycle" && live[0]["fields"]["speed"] == 5.0, "live fields readable");
    scripts.SetScriptField(lifecycleId, 0, "speed", 9.0);
    live = scripts.EntityScripts(lifecycleId);
    Expect(live.size() == 1 && live[0]["fields"]["speed"] == 9.0, "live field editable");

    // The editor deleting an object while playing runs its OnDisable/OnDestroy and removes it.
    scripts.DestroyEntity(moverId);
    Expect(scene.Get(moverId) == nullptr, "editor delete during play");

    scripts.EndPlay();
    physics.Stop();

    // ---- Hot reload keeps working.
    Expect(CompileAndWait(scripts, files) == ScriptEngine::CompileState::Succeeded && scripts.Version() == version + 1,
           "recompile / hot reload");

    // ---- What the scripts reported
    std::set<std::string> passed;
    std::vector<std::string> failures;
    int unexpectedErrors = 0, expectedException = 0, missingScriptWarnings = 0;
    std::regex ok(R"(^CHECK ([A-Za-z0-9_.]+) ok$)");
    Log::ForEachEntry([&](const LogEntry& entry) {
        std::smatch m;
        if (std::regex_match(entry.message, m, ok))
            passed.insert(m[1]);
        if (entry.message.rfind("FAIL ", 0) == 0)
            failures.push_back(entry.message);
        else if (entry.level == LogLevel::Error) {
            if (entry.message.find("expected test exception") != std::string::npos)
                ++expectedException;
            else if (entry.message.find("CS0103") == std::string::npos &&
                     entry.message.find("compile errors") == std::string::npos)
                ++unexpectedErrors;
        }
        if (entry.level == LogLevel::Warning && entry.message.find("NoSuchScript") != std::string::npos)
            ++missingScriptWarnings;
    });

    for (const std::string& f : failures)
        Expect(false, f);
    std::set<std::string> expected = ExpectedChecks(scriptsDir);
    for (const std::string& name : expected)
        if (!passed.contains(name))
            Expect(false, "check did not run: " + name);
    Expect(failures.empty() && passed.size() >= expected.size(),
           std::to_string(passed.size()) + "/" + std::to_string(expected.size()) + " script checks passed");
    Expect(expectedException == 1, "exception in Update is logged with the script, not fatal");
    Expect(missingScriptWarnings == 1, "missing script class is reported");
    Expect(unexpectedErrors == 0, "no unexpected errors");

    std::printf("\n%s (%d failure(s))\n", g_Failures ? "FAILED" : "PASSED", g_Failures);
    return g_Failures ? 1 : 0;
}
