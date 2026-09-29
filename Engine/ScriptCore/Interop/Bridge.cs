using System;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text.Json.Nodes;
using IndeetsEngine.Runtime;
using UnityEngine;

namespace IndeetsEngine.Interop
{
    /// <summary>
    /// Entry points the native engine calls (resolved by name through hostfxr). None of them may
    /// let an exception escape into native code, so each one catches and logs.
    /// </summary>
    public static unsafe class Bridge
    {
        private static void Report(Exception e) => Native.Log(2, "[Scripting] " + World.DescribeException(e, false));

        private static JsonNode ParseJson(byte* utf8)
        {
            string text = Native.FromUtf8(utf8);
            return string.IsNullOrWhiteSpace(text) ? null : JsonNode.Parse(text);
        }

        [UnmanagedCallersOnly]
        public static int Initialize(NativeApi* api, int isEditor)
        {
            try
            {
                Native.Attach(api);
                Application.isEditor = isEditor != 0;
                ScriptCompiler.Warmup();
                return 1;
            }
            catch (Exception e)
            {
                Report(e);
                return 0;
            }
        }

        /// <summary>{"dataPath": "...", "productName": "...", "companyName": "..."}</summary>
        [UnmanagedCallersOnly]
        public static void SetApplicationInfo(byte* json)
        {
            try
            {
                JsonNode info = ParseJson(json);
                if (info?["dataPath"] is JsonNode dataPath) Application.dataPath = dataPath.GetValue<string>();
                if (info?["productName"] is JsonNode product) Application.productName = product.GetValue<string>();
                if (info?["companyName"] is JsonNode company) Application.companyName = company.GetValue<string>();
            }
            catch (Exception e)
            {
                Report(e);
            }
        }

        /// <summary>{"files": ["a.cs", ...], "assemblyName": "Assembly-CSharp"}. Returns 0 if a compile is already running.</summary>
        [UnmanagedCallersOnly]
        public static int CompileAsync(byte* requestJson)
        {
            try
            {
                JsonNode request = ParseJson(requestJson);
                string[] files = (request?["files"] as JsonArray)?.Select(n => n.GetValue<string>()).ToArray() ?? Array.Empty<string>();
                string name = request?["assemblyName"]?.GetValue<string>() ?? "Assembly-CSharp";
                return ScriptDomain.StartCompile(files, name) ? 1 : 0;
            }
            catch (Exception e)
            {
                Report(e);
                return 0;
            }
        }

        /// <summary>0 idle, 1 compiling, 2 finished and loaded, 3 finished with errors. On 2/3 the result holds the diagnostics.</summary>
        [UnmanagedCallersOnly]
        public static int PollCompile()
        {
            try
            {
                int state = ScriptDomain.Poll(out CompileResult result);
                if (result != null)
                {
                    var diagnostics = new JsonArray();
                    foreach (JsonObject d in result.Diagnostics)
                        diagnostics.Add(d.DeepClone());
                    Native.SetResult(new JsonObject
                    {
                        ["success"] = state == 2,
                        ["seconds"] = result.Seconds,
                        ["scriptCount"] = ScriptDomain.ScriptTypes.Count,
                        ["diagnostics"] = diagnostics,
                    }.ToJsonString());
                }
                return state;
            }
            catch (Exception e)
            {
                Report(e);
                return 3;
            }
        }

        /// <summary>Loads a prebuilt game assembly (standalone players).</summary>
        [UnmanagedCallersOnly]
        public static int LoadAssembly(byte* path)
        {
            try
            {
                ScriptDomain.LoadFromFile(Native.FromUtf8(path));
                return 1;
            }
            catch (Exception e)
            {
                Report(e);
                return 0;
            }
        }

        [UnmanagedCallersOnly]
        public static int DescribeScripts()
        {
            try
            {
                Native.SetResult(ScriptDomain.DescribeScripts().ToJsonString());
                return ScriptDomain.ScriptTypes.Count;
            }
            catch (Exception e)
            {
                Report(e);
                Native.SetResult("[]");
                return 0;
            }
        }

        /// <summary>Scene scripts: {"entities": [{"id": 1, "scripts": [{"class": "Mover", "enabled": true, "fields": {...}}]}]}.</summary>
        [UnmanagedCallersOnly]
        public static int BeginPlay(byte* sceneJson)
        {
            try
            {
                World.BeginPlay(ParseJson(sceneJson));
                return 1;
            }
            catch (Exception e)
            {
                Report(e);
                return 0;
            }
        }

        [UnmanagedCallersOnly]
        public static void FixedUpdate(float fixedDeltaTime)
        {
            try { World.FixedUpdate(fixedDeltaTime); }
            catch (Exception e) { Report(e); }
        }

        [UnmanagedCallersOnly]
        public static void Update(float unscaledDeltaTime)
        {
            try { World.Update(unscaledDeltaTime); }
            catch (Exception e) { Report(e); }
        }

        [UnmanagedCallersOnly]
        public static void LateUpdate()
        {
            try { World.LateUpdate(); }
            catch (Exception e) { Report(e); }
        }

        [UnmanagedCallersOnly]
        public static void EndPlay()
        {
            try { World.EndPlay(); }
            catch (Exception e) { Report(e); }
        }

        [UnmanagedCallersOnly]
        public static float GetTimeScale() => Time.timeScale;

        [UnmanagedCallersOnly]
        public static int ConsumeQuitRequest()
        {
            try { return World.ConsumeQuitRequest() ? 1 : 0; }
            catch { return 0; }
        }

        /// <summary>Destroys an entity the editor deleted during play, running OnDisable / OnDestroy.</summary>
        [UnmanagedCallersOnly]
        public static void DestroyEntity(uint id)
        {
            try { World.DestroyGameObject(id, true); }
            catch (Exception e) { Report(e); }
        }

        /// <summary>Live scripts of an entity: [{"class", "enabled", "fields"}].</summary>
        [UnmanagedCallersOnly]
        public static int GetEntityScripts(uint id)
        {
            try
            {
                var array = new JsonArray();
                foreach (MonoBehaviour b in World.ScriptsOn(id))
                {
                    array.Add(new JsonObject
                    {
                        ["class"] = ScriptDomain.StoredName(b.GetType()),
                        ["enabled"] = b.enabled,
                        ["fields"] = FieldCodec.WriteObject(b),
                    });
                }
                Native.SetResult(array.ToJsonString());
                return array.Count;
            }
            catch (Exception e)
            {
                Report(e);
                Native.SetResult("[]");
                return 0;
            }
        }

        [UnmanagedCallersOnly]
        public static int SetScriptField(uint id, int index, byte* name, byte* valueJson)
        {
            try
            {
                var scripts = World.ScriptsOn(id);
                if (index < 0 || index >= scripts.Count)
                    return 0;
                string field = Native.FromUtf8(name);
                FieldCodec.ReadInto(scripts[index], new JsonObject { [field] = ParseJson(valueJson) }, null);
                return 1;
            }
            catch (Exception e)
            {
                Report(e);
                return 0;
            }
        }

        [UnmanagedCallersOnly]
        public static int SetScriptEnabled(uint id, int index, int enabled)
        {
            try
            {
                var scripts = World.ScriptsOn(id);
                if (index < 0 || index >= scripts.Count)
                    return 0;
                scripts[index].enabled = enabled != 0;
                return 1;
            }
            catch (Exception e)
            {
                Report(e);
                return 0;
            }
        }

        [UnmanagedCallersOnly]
        public static int AddScript(uint id, byte* className, byte* fieldsJson)
        {
            try
            {
                Type type = ScriptDomain.FindScriptType(Native.FromUtf8(className));
                GameObject go = World.GetGameObject(id);
                if (type == null || go == null)
                    return 0;
                var behaviour = (MonoBehaviour)World.AddComponent(go, type);
                if (behaviour != null && ParseJson(fieldsJson) is JsonObject fields)
                    FieldCodec.ReadInto(behaviour, fields, null);
                return behaviour != null ? 1 : 0;
            }
            catch (Exception e)
            {
                Report(e);
                return 0;
            }
        }

        [UnmanagedCallersOnly]
        public static int RemoveScript(uint id, int index)
        {
            try
            {
                var scripts = World.ScriptsOn(id);
                if (index < 0 || index >= scripts.Count)
                    return 0;
                World.DestroyNow(scripts[index]);
                return 1;
            }
            catch (Exception e)
            {
                Report(e);
                return 0;
            }
        }
    }
}
