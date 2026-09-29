using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.Loader;
using System.Text;
using System.Text.Json.Nodes;
using System.Threading.Tasks;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.Emit;
using Microsoft.CodeAnalysis.Text;
using UnityEngine;

namespace IndeetsEngine.Runtime
{
    /// <summary>Collectible context for the compiled game scripts, so they can be hot-reloaded.</summary>
    internal sealed class GameLoadContext : AssemblyLoadContext
    {
        public GameLoadContext() : base("IndeetsEngine.GameScripts", isCollectible: true) { }

        protected override Assembly Load(AssemblyName name)
        {
            // Scripts must bind to the one ScriptCore instance the engine is already running.
            if (name.Name == typeof(World).Assembly.GetName().Name)
                return typeof(World).Assembly;
            return null;
        }
    }

    internal sealed class CompileResult
    {
        public bool Success;
        public byte[] Assembly;
        public byte[] Pdb;
        public readonly List<JsonObject> Diagnostics = new List<JsonObject>();
        public double Seconds;
    }

    /// <summary>C# compiler for project scripts (Roslyn, in process; no .NET SDK needed).</summary>
    internal static class ScriptCompiler
    {
        private static List<MetadataReference> s_References;
        private static readonly object s_Lock = new object();

        // Unity's standard defines, so version checks in existing scripts take the modern branch.
        private static readonly string[] Defines =
        {
            "INDEETS_ENGINE", "UNITY_5_3_OR_NEWER", "UNITY_2017_1_OR_NEWER", "UNITY_2018_1_OR_NEWER", "UNITY_2019_1_OR_NEWER",
            "UNITY_2020_1_OR_NEWER", "UNITY_2021_1_OR_NEWER", "UNITY_2022_1_OR_NEWER", "UNITY_2023_1_OR_NEWER",
            "UNITY_6000_0_OR_NEWER", "UNITY_64", "UNITY_STANDALONE", "ENABLE_INPUT_SYSTEM", "ENABLE_LEGACY_INPUT_MANAGER",
            "DEBUG", "TRACE",
        };

        // Warnings Unity hides for serialized fields and that would only be noise here.
        private static readonly string[] SuppressedWarnings = { "CS0649", "CS8632", "CS0414", "CS0169" };

        private static List<MetadataReference> References()
        {
            lock (s_Lock)
            {
                if (s_References != null)
                    return s_References;
                var refs = new List<MetadataReference>();
                var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                string tpa = AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES") as string ?? string.Empty;
                foreach (string path in tpa.Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries))
                {
                    string file = Path.GetFileName(path);
                    if (!file.EndsWith(".dll", StringComparison.OrdinalIgnoreCase) || !seen.Add(file))
                        continue;
                    if (!(file.StartsWith("System.", StringComparison.OrdinalIgnoreCase) ||
                          file.Equals("System.dll", StringComparison.OrdinalIgnoreCase) ||
                          file.Equals("mscorlib.dll", StringComparison.OrdinalIgnoreCase) ||
                          file.Equals("netstandard.dll", StringComparison.OrdinalIgnoreCase) ||
                          file.StartsWith("Microsoft.CSharp", StringComparison.OrdinalIgnoreCase) ||
                          file.StartsWith("Microsoft.Win32.", StringComparison.OrdinalIgnoreCase)))
                        continue;
                    try
                    {
                        refs.Add(MetadataReference.CreateFromFile(path));
                    }
                    catch
                    {
                        // Not a managed assembly.
                    }
                }
                refs.Add(MetadataReference.CreateFromFile(typeof(World).Assembly.Location));
                s_References = refs;
                return refs;
            }
        }

        private static bool IsEditorOnly(string path)
        {
            string[] parts = path.Replace('\\', '/').Split('/');
            // Unity: scripts inside any folder named "Editor" belong to the editor assembly.
            for (int i = 0; i < parts.Length - 1; i++)
                if (parts[i] == "Editor")
                    return true;
            return false;
        }

        public static CompileResult Compile(IReadOnlyList<string> files, string assemblyName)
        {
            var watch = System.Diagnostics.Stopwatch.StartNew();
            var result = new CompileResult();
            var parseOptions = new CSharpParseOptions(LanguageVersion.Latest, DocumentationMode.None, SourceCodeKind.Regular,
                Defines.Concat(OperatingSystem.IsWindows() ? new[] { "UNITY_STANDALONE_WIN" } :
                               OperatingSystem.IsMacOS() ? new[] { "UNITY_STANDALONE_OSX" } : new[] { "UNITY_STANDALONE_LINUX" }));
            var trees = new List<SyntaxTree>();
            foreach (string path in files)
            {
                if (IsEditorOnly(path))
                    continue;
                try
                {
                    SourceText text;
                    using (FileStream stream = File.OpenRead(path))
                        text = SourceText.From(stream, Encoding.UTF8, canBeEmbedded: true);
                    trees.Add(CSharpSyntaxTree.ParseText(text, parseOptions, path));
                }
                catch (Exception e)
                {
                    result.Diagnostics.Add(new JsonObject
                    {
                        ["severity"] = "error", ["file"] = path, ["line"] = 0, ["column"] = 0, ["id"] = "IO",
                        ["message"] = e.Message,
                    });
                }
            }

            var options = new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary,
                    optimizationLevel: OptimizationLevel.Debug, allowUnsafe: true, concurrentBuild: true,
                    nullableContextOptions: NullableContextOptions.Disable)
                .WithSpecificDiagnosticOptions(SuppressedWarnings.ToDictionary(id => id, _ => ReportDiagnostic.Suppress));

            CSharpCompilation compilation = CSharpCompilation.Create(assemblyName + "-" + Guid.NewGuid().ToString("N").Substring(0, 8),
                trees, References(), options);

            using var pe = new MemoryStream();
            using var pdb = new MemoryStream();
            EmitResult emit = compilation.Emit(pe, pdb,
                embeddedTexts: trees.Select(t => EmbeddedText.FromSource(t.FilePath, t.GetText())),
                options: new EmitOptions(debugInformationFormat: DebugInformationFormat.PortablePdb));

            int warnings = 0;
            foreach (Diagnostic d in emit.Diagnostics)
            {
                if (d.Severity != DiagnosticSeverity.Error && d.Severity != DiagnosticSeverity.Warning)
                    continue;
                if (d.Severity == DiagnosticSeverity.Warning && ++warnings > 100)
                    continue;
                FileLinePositionSpan span = d.Location.GetLineSpan();
                result.Diagnostics.Add(new JsonObject
                {
                    ["severity"] = d.Severity == DiagnosticSeverity.Error ? "error" : "warning",
                    ["file"] = span.Path ?? string.Empty,
                    ["line"] = span.StartLinePosition.Line + 1,
                    ["column"] = span.StartLinePosition.Character + 1,
                    ["id"] = d.Id,
                    ["message"] = d.GetMessage(),
                });
            }

            result.Success = emit.Success && !result.Diagnostics.Any(d => (string)d["severity"] == "error");
            if (result.Success)
            {
                result.Assembly = pe.ToArray();
                result.Pdb = pdb.ToArray();
            }
            result.Seconds = watch.Elapsed.TotalSeconds;
            return result;
        }

        /// <summary>Loads Roslyn's code paths once in the background, so the first real compile is quick.</summary>
        public static void Warmup() => Task.Run(() =>
        {
            try
            {
                References();
                CSharpCompilation.Create("Warmup", new[] { CSharpSyntaxTree.ParseText("class W { }") }, References(),
                    new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary)).Emit(new MemoryStream());
            }
            catch
            {
                // Only a warm-up.
            }
        });
    }

    /// <summary>The loaded game assembly and the script types it provides.</summary>
    internal static class ScriptDomain
    {
        private static GameLoadContext s_Context;
        private static Assembly s_Assembly;
        private static readonly Dictionary<string, Type> s_ByFullName = new Dictionary<string, Type>();
        private static readonly Dictionary<string, Type> s_ByName = new Dictionary<string, Type>();
        private static readonly List<Type> s_ScriptTypes = new List<Type>();

        private static Task<CompileResult> s_Pending;

        public static bool IsCompiling => s_Pending != null;
        public static bool HasAssembly => s_Assembly != null;

        public static bool StartCompile(string[] files, string assemblyName)
        {
            if (s_Pending != null)
                return false;
            s_Pending = Task.Run(() => ScriptCompiler.Compile(files, assemblyName));
            return true;
        }

        /// <summary>0 = nothing pending, 1 = still compiling, 2 = done and loaded, 3 = done with errors.</summary>
        public static int Poll(out CompileResult result)
        {
            result = null;
            if (s_Pending == null)
                return 0;
            if (!s_Pending.IsCompleted)
                return 1;
            try
            {
                result = s_Pending.Result;
            }
            catch (Exception e)
            {
                result = new CompileResult();
                result.Diagnostics.Add(new JsonObject
                {
                    ["severity"] = "error", ["file"] = string.Empty, ["line"] = 0, ["column"] = 0, ["id"] = "Compiler",
                    ["message"] = World.DescribeException(e),
                });
            }
            s_Pending = null;
            if (!result.Success)
                return 3;
            try
            {
                Load(result.Assembly, result.Pdb);
            }
            catch (Exception e)
            {
                result.Success = false;
                result.Diagnostics.Add(new JsonObject
                {
                    ["severity"] = "error", ["file"] = string.Empty, ["line"] = 0, ["column"] = 0, ["id"] = "Load",
                    ["message"] = World.DescribeException(e),
                });
                return 3;
            }
            return 2;
        }

        public static void Load(byte[] assembly, byte[] pdb)
        {
            Unload();
            s_Context = new GameLoadContext();
            using (var pe = new MemoryStream(assembly))
            using (var symbols = pdb != null ? new MemoryStream(pdb) : null)
                s_Assembly = s_Context.LoadFromStream(pe, symbols);
            IndexTypes();
        }

        public static void LoadFromFile(string path)
        {
            Unload();
            s_Context = new GameLoadContext();
            s_Assembly = s_Context.LoadFromAssemblyPath(Path.GetFullPath(path));
            IndexTypes();
        }

        private static void IndexTypes()
        {
            Type[] types;
            try
            {
                types = s_Assembly.GetTypes();
            }
            catch (ReflectionTypeLoadException e)
            {
                types = e.Types.Where(t => t != null).ToArray();
            }
            foreach (Type t in types)
            {
                if (t.IsAbstract || t.ContainsGenericParameters || !typeof(MonoBehaviour).IsAssignableFrom(t))
                    continue;
                s_ScriptTypes.Add(t);
                s_ByFullName[t.FullName ?? t.Name] = t;
                if (s_ByName.ContainsKey(t.Name))
                    s_ByName[t.Name] = null; // ambiguous: needs the full name
                else
                    s_ByName[t.Name] = t;
            }
            s_ScriptTypes.Sort((a, b) => string.CompareOrdinal(a.FullName, b.FullName));
        }

        public static void Unload()
        {
            s_ScriptTypes.Clear();
            s_ByFullName.Clear();
            s_ByName.Clear();
            World.ClearTypeCaches();
            s_Assembly = null;
            if (s_Context != null)
            {
                s_Context.Unload();
                s_Context = null;
            }
        }

        public static IReadOnlyList<Type> ScriptTypes => s_ScriptTypes;

        /// <summary>The name a script is stored under: the class name, or the full name if it is ambiguous.</summary>
        public static string StoredName(Type t) =>
            s_ByName.TryGetValue(t.Name, out Type unique) && unique == t ? t.Name : t.FullName;

        public static Type FindScriptType(string name)
        {
            if (string.IsNullOrEmpty(name))
                return null;
            if (s_ByFullName.TryGetValue(name, out Type t))
                return t;
            return s_ByName.TryGetValue(name, out t) ? t : null;
        }

        public static IEnumerable<MethodInfo> InitializeMethods(RuntimeInitializeLoadType when)
        {
            if (s_Assembly == null)
                yield break;
            Type[] types;
            try
            {
                types = s_Assembly.GetTypes();
            }
            catch (ReflectionTypeLoadException e)
            {
                types = e.Types.Where(t => t != null).ToArray();
            }
            const BindingFlags Flags = BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic;
            foreach (Type t in types)
            {
                if (t.ContainsGenericParameters)
                    continue;
                foreach (MethodInfo m in t.GetMethods(Flags))
                {
                    var attr = m.GetCustomAttribute<RuntimeInitializeOnLoadMethodAttribute>();
                    if (attr != null && attr.loadType == when && m.GetParameters().Length == 0)
                        yield return m;
                }
            }
        }

        /// <summary>Script types with their inspector field descriptions.</summary>
        public static JsonArray DescribeScripts()
        {
            var array = new JsonArray();
            foreach (Type t in s_ScriptTypes)
            {
                array.Add(new JsonObject
                {
                    ["name"] = StoredName(t),
                    ["displayName"] = FieldCodec.NicifyName(t.Name),
                    ["fullName"] = t.FullName,
                    ["fields"] = FieldCodec.DescribeFields(t, 0),
                });
            }
            return array;
        }
    }
}
