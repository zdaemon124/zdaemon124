using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json.Nodes;
using IndeetsEngine.Interop;
using UnityEngine;
using Object = UnityEngine.Object;

namespace IndeetsEngine.Runtime
{
    /// <summary>
    /// Project assets for scripts: Resources.Load and asset references in serialized fields
    /// ({"asset": path, "fileID", "name"}). Every asset is loaded once per play session, so all
    /// references to it share one object, like Unity. Prefabs become hidden, inactive template
    /// entities (never awake) that Instantiate clones.
    /// </summary>
    internal static unsafe class AssetLoader
    {
        private sealed class AssetKey
        {
            public string Path;
            public long FileId;
            public string SubName;
        }

        private static readonly Dictionary<string, Object> s_Cache = new Dictionary<string, Object>();
        private static readonly Dictionary<string, JsonObject> s_Info = new Dictionary<string, JsonObject>();
        private static readonly Dictionary<Object, AssetKey> s_Keys = new Dictionary<Object, AssetKey>();
        // Objects that are neither entities nor assets (ScriptableObject.CreateInstance, new Material ...)
        // referenced by serialized fields: kept by instance id so Instantiate copies the reference.
        private static readonly Dictionary<int, WeakReference<Object>> s_Runtime = new Dictionary<int, WeakReference<Object>>();

        public static void Reset()
        {
            s_Cache.Clear();
            s_Info.Clear();
            s_Keys.Clear();
            s_Runtime.Clear();
        }

        // ================================================================ native queries

        private static JsonObject Describe(string path)
        {
            if (s_Info.TryGetValue(path, out JsonObject info))
                return info;
            info = null;
            if (Native.IsAvailable)
            {
                fixed (byte* p = Native.Utf8(path))
                {
                    try { info = JsonNode.Parse(Native.FromUtf8(Native.Api.AssetDescribe(p))) as JsonObject; }
                    catch { info = null; }
                }
            }
            info ??= new JsonObject { ["kind"] = "missing" };
            s_Info[path] = info;
            return info;
        }

        private static string Kind(string path) => Describe(path)["kind"]?.GetValue<string>() ?? "missing";

        public static List<string> FindResources(string path, bool all)
        {
            var result = new List<string>();
            if (!Native.IsAvailable)
                return result;
            path = (path ?? string.Empty).Replace('\\', '/').Trim('/');
            fixed (byte* p = Native.Utf8(path))
            {
                if (JsonNode.Parse(Native.FromUtf8(Native.Api.AssetFindResources(p, all ? 1 : 0))) is JsonArray array)
                    foreach (JsonNode item in array)
                        if (item != null)
                            result.Add(item.GetValue<string>());
            }
            return result;
        }

        // ================================================================ loading

        /// <summary>Whether an asset of `kind` can be loaded as `type`.</summary>
        private static bool Compatible(string kind, Type type)
        {
            if (type == null || type == typeof(Object))
                return kind != "missing" && kind != "other" && kind != "script-missing";
            switch (kind)
            {
                case "prefab":
                case "model":
                    return type == typeof(GameObject) || typeof(Component).IsAssignableFrom(type) || type.IsInterface;
                case "script":
                    return typeof(ScriptableObject).IsAssignableFrom(type) || type.IsInterface;
                case "texture":
                    return type == typeof(Texture) || type == typeof(Texture2D) || type == typeof(Sprite);
                case "material":
                    return type == typeof(Material);
                case "audio":
                    return type == typeof(AudioClip);
                case "text":
                    return type == typeof(TextAsset);
                default:
                    return false;
            }
        }

        /// <summary>Loads an asset as `type` (null: its natural type). Returns null when it cannot be.</summary>
        public static Object Load(string path, Type type, long fileId = 0, string subName = null)
        {
            if (string.IsNullOrEmpty(path))
                return null;
            string kind = Kind(path);
            if (!Compatible(kind, type))
                return null;

            // A sprite sheet's entries are sub-assets, told apart by their name.
            bool sprite = type == typeof(Sprite) || (type == null && kind == "texture" && !string.IsNullOrEmpty(subName));
            string cacheKey = sprite ? $"{path}#sprite#{subName}" : kind == "texture" ? $"{path}#texture" : path;
            if (!s_Cache.TryGetValue(cacheKey, out Object asset) || !asset)
            {
                asset = Create(path, kind, sprite, subName);
                if (asset is null)
                    return null;
                s_Cache[cacheKey] = asset;
                s_Keys[asset] = new AssetKey { Path = path, FileId = fileId, SubName = subName };
            }
            return Cast(asset, type);
        }

        private static Object Cast(Object asset, Type type)
        {
            if (type == null || type.IsInstanceOfType(asset))
                return asset;
            if (asset is GameObject go && (typeof(Component).IsAssignableFrom(type) || type.IsInterface))
                return World.GetComponent(go, type) ?? World.FindInHierarchy(go, type, true, true);
            return null;
        }

        private static Object Create(string path, string kind, bool sprite, string subName)
        {
            string name = Describe(path)["name"]?.GetValue<string>() ?? Path.GetFileNameWithoutExtension(path);
            switch (kind)
            {
                case "prefab":
                case "model":
                    return LoadPrefab(path);
                case "script":
                    return LoadScriptable(path);
                case "texture":
                {
                    Texture2D texture = sprite ? (Texture2D)Load(path, typeof(Texture2D)) : new Texture2D { name = name, m_AssetPath = path };
                    if (!sprite)
                        return texture;
                    return new Sprite { texture = texture, name = string.IsNullOrEmpty(subName) ? name : subName, pivot = new Vector2(0.5f, 0.5f) };
                }
                case "material":
                {
                    JsonObject info = Describe(path);
                    var material = new Material((Shader)null) { name = name };
                    if (info["color"] is JsonArray c && c.Count >= 4)
                        material.color = new Color((float)FieldCodec.Number(c[0]), (float)FieldCodec.Number(c[1]),
                                                   (float)FieldCodec.Number(c[2]), (float)FieldCodec.Number(c[3]));
                    return material;
                }
                case "audio":
                    return new AudioClip { name = name, m_AssetPath = path };
                case "text":
                {
                    byte[] bytes;
                    try { bytes = File.ReadAllBytes(Path.Combine(Application.dataPath, path)); }
                    catch (Exception e)
                    {
                        Debug.LogWarning($"Text asset '{path}' could not be read: {e.Message}");
                        return null;
                    }
                    return new TextAsset(bytes) { name = name };
                }
                default:
                    return null;
            }
        }

        private static GameObject LoadPrefab(string path)
        {
            uint root;
            fixed (byte* p = Native.Utf8(path))
                root = Native.Api.AssetPrefabTemplate(p);
            if (root == 0)
            {
                Debug.LogWarning($"Prefab '{path}' could not be loaded.");
                return null;
            }
            GameObject go = World.GetGameObject(root);
            // Cached before its scripts read their fields: prefabs may reference each other.
            s_Cache[path] = go;
            s_Keys[go] = new AssetKey { Path = path };
            uint[] subtree = World.Subtree(root);
            foreach (uint id in subtree)
            {
                GameObject member = World.GetGameObject(id);
                if (member != null)
                    member.m_IsAsset = true;
            }
            World.CreateStoredScripts(subtree);
            return go;
        }

        private static ScriptableObject LoadScriptable(string path)
        {
            JsonObject info = Describe(path);
            string className = info["class"]?.GetValue<string>();
            Type type = ScriptDomain.FindType(className);
            if (type == null || !typeof(ScriptableObject).IsAssignableFrom(type) || type.IsAbstract)
            {
                Debug.LogWarning($"ScriptableObject '{path}': script '{className}' is missing or is not a ScriptableObject.");
                return null;
            }
            ScriptableObject so;
            try
            {
                so = (ScriptableObject)Activator.CreateInstance(type, true);
            }
            catch (Exception e)
            {
                Debug.LogError($"Could not create '{className}': {World.DescribeException(e)}");
                return null;
            }
            so.name = info["name"]?.GetValue<string>() ?? Path.GetFileNameWithoutExtension(path);
            // Cached before reading fields: assets may reference each other (or themselves).
            s_Cache[path] = so;
            s_Keys[so] = new AssetKey { Path = path, FileId = 11400000 };
            if (info["fields"] is JsonObject fields)
                FieldCodec.ReadInto(so, fields, null);
            World.InvokeMessage(so, "Awake");
            World.InvokeMessage(so, "OnEnable");
            return so;
        }

        // ================================================================ serialized references

        /// <summary>Reads {"asset": path, "fileID", "name"} as an object of `type`.</summary>
        public static Object Resolve(JsonObject reference, Type type)
        {
            string path = reference["asset"]?.GetValue<string>();
            long fileId = (long)FieldCodec.Number(reference["fileID"]);
            string subName = reference["name"]?.GetValue<string>();
            return Load(path, type == typeof(Object) ? null : type, fileId, subName);
        }

        /// <summary>Reads {"instance": id}: a runtime object another script's field referenced.</summary>
        public static Object ResolveRuntime(JsonObject reference)
        {
            int id = (int)FieldCodec.Number(reference["instance"]);
            return s_Runtime.TryGetValue(id, out WeakReference<Object> weak) && weak.TryGetTarget(out Object o) && o ? o : null;
        }

        /// <summary>The serialized form of a reference to a non-entity object (asset or runtime object).</summary>
        public static JsonObject Reference(Object o)
        {
            if (s_Keys.TryGetValue(o, out AssetKey key))
            {
                var json = new JsonObject { ["asset"] = key.Path, ["fileID"] = key.FileId };
                if (!string.IsNullOrEmpty(key.SubName))
                    json["name"] = key.SubName;
                return json;
            }
            s_Runtime[o.GetInstanceID()] = new WeakReference<Object>(o);
            return new JsonObject { ["instance"] = o.GetInstanceID() };
        }

        // ================================================================ Resources

        public static Object ResourcesLoad(string path, Type type)
        {
            foreach (string asset in FindResources(path, false))
            {
                Object o = Load(asset, type);
                if (o)
                    return o;
            }
            return null;
        }

        public static List<Object> ResourcesLoadAll(string path, Type type)
        {
            var result = new List<Object>();
            foreach (string asset in FindResources(path, true))
            {
                Object o = Load(asset, type);
                if (o)
                    result.Add(o);
            }
            return result;
        }
    }
}
