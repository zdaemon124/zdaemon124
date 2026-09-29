using System;
using System.Collections;
using System.Collections.Generic;
using System.Reflection;
using System.Text;
using System.Text.Json.Nodes;
using UnityEngine;
using Object = UnityEngine.Object;

namespace IndeetsEngine.Runtime
{
    /// <summary>
    /// Serialized script fields, following Unity's rules: public or [SerializeField] instance
    /// fields of supported types (primitives, enums, math structs, scene object references,
    /// arrays / List&lt;T&gt; and [Serializable] classes or structs).
    ///
    /// Values are exchanged with the engine as JSON: numbers, strings, arrays of floats for
    /// vectors and colors, {"entity": id} for GameObject / component references.
    /// </summary>
    internal static class FieldCodec
    {
        private static readonly Dictionary<Type, FieldInfo[]> s_Fields = new Dictionary<Type, FieldInfo[]>();
        private static readonly Dictionary<Type, bool> s_Supported = new Dictionary<Type, bool>();
        private const int MaxDepth = 7;

        public static void ClearCaches()
        {
            s_Fields.Clear();
            s_Supported.Clear();
        }

        private static bool IsEngineBase(Type t) =>
            t == null || t == typeof(object) || t == typeof(MonoBehaviour) || t == typeof(Behaviour) ||
            t == typeof(Component) || t == typeof(Object) || t == typeof(ScriptableObject) || t == typeof(ValueType);

        /// <summary>Serialized fields of a type, base class fields first (as the Unity Inspector shows them).</summary>
        public static FieldInfo[] SerializedFields(Type type)
        {
            if (s_Fields.TryGetValue(type, out FieldInfo[] cached))
                return cached;
            var chain = new List<Type>();
            for (Type t = type; !IsEngineBase(t); t = t.BaseType)
                chain.Insert(0, t);
            var fields = new List<FieldInfo>();
            const BindingFlags Flags = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly;
            foreach (Type t in chain)
            {
                foreach (FieldInfo f in t.GetFields(Flags))
                {
                    if (f.IsInitOnly || f.IsLiteral || (f.Attributes & FieldAttributes.NotSerialized) != 0)
                        continue;
                    if (f.Name.Contains('<')) // compiler-generated backing fields
                        continue;
                    bool serialize = f.IsPublic || f.GetCustomAttribute<SerializeField>() != null ||
                                     f.GetCustomAttribute<SerializeReference>() != null;
                    if (serialize && IsSupported(f.FieldType, 0))
                        fields.Add(f);
                }
            }
            cached = fields.ToArray();
            s_Fields[type] = cached;
            return cached;
        }

        private static bool IsSerializableStruct(Type t) =>
            !t.IsPrimitive && !t.IsEnum && t != typeof(string) && !typeof(Object).IsAssignableFrom(t) &&
            !t.IsAbstract && !t.IsInterface && !t.ContainsGenericParameters &&
            t.GetCustomAttribute<SerializableAttribute>() != null && !typeof(Delegate).IsAssignableFrom(t);

        private static Type ElementType(Type t)
        {
            if (t.IsArray && t.GetArrayRank() == 1)
                return t.GetElementType();
            if (t.IsGenericType && t.GetGenericTypeDefinition() == typeof(List<>))
                return t.GetGenericArguments()[0];
            return null;
        }

        public static bool IsSupported(Type t, int depth)
        {
            if (depth > MaxDepth)
                return false;
            if (depth == 0 && s_Supported.TryGetValue(t, out bool cached))
                return cached;
            bool ok;
            if (t.IsPrimitive || t == typeof(string) || t.IsEnum || t == typeof(decimal))
                ok = t != typeof(IntPtr) && t != typeof(UIntPtr);
            else if (t == typeof(Vector2) || t == typeof(Vector3) || t == typeof(Vector4) || t == typeof(Quaternion) ||
                     t == typeof(Color) || t == typeof(Color32) || t == typeof(Vector2Int) || t == typeof(Vector3Int) ||
                     t == typeof(Rect) || t == typeof(Bounds) || t == typeof(LayerMask))
                ok = true;
            else if (typeof(Object).IsAssignableFrom(t))
                ok = true;
            else if (ElementType(t) is Type element)
                ok = ElementType(element) == null && IsSupported(element, depth + 1); // no nested collections, like Unity
            else
                ok = IsSerializableStruct(t);
            if (depth == 0)
                s_Supported[t] = ok;
            return ok;
        }

        // ================================================================ write

        public static JsonObject WriteObject(object instance)
        {
            var obj = new JsonObject();
            if (instance == null)
                return obj;
            foreach (FieldInfo f in SerializedFields(instance.GetType()))
            {
                try
                {
                    obj[f.Name] = Write(f.FieldType, f.GetValue(instance), 0);
                }
                catch (Exception e)
                {
                    Debug.LogWarning($"Could not serialize field '{f.Name}': {e.Message}");
                }
            }
            return obj;
        }

        private static JsonArray Floats(params float[] values)
        {
            var a = new JsonArray();
            foreach (float v in values)
                a.Add(float.IsFinite(v) ? v : 0f);
            return a;
        }

        public static JsonNode Write(Type t, object value, int depth)
        {
            if (depth > MaxDepth)
                return null;
            if (t == typeof(string))
                return JsonValue.Create((string)value ?? string.Empty);
            if (t == typeof(bool))
                return JsonValue.Create((bool)value);
            if (t.IsEnum)
                return JsonValue.Create(Convert.ToInt64(value));
            if (t == typeof(float))
                return JsonValue.Create(float.IsFinite((float)value) ? (float)value : 0f);
            if (t == typeof(double))
                return JsonValue.Create(double.IsFinite((double)value) ? (double)value : 0.0);
            if (t == typeof(decimal))
                return JsonValue.Create((double)(decimal)value);
            if (t == typeof(char))
                return JsonValue.Create(((char)value).ToString());
            if (t.IsPrimitive)
                return JsonValue.Create(Convert.ToInt64(value));
            switch (value)
            {
                case Vector2 v: return Floats(v.x, v.y);
                case Vector3 v: return Floats(v.x, v.y, v.z);
                case Vector4 v: return Floats(v.x, v.y, v.z, v.w);
                case Quaternion q: return Floats(q.x, q.y, q.z, q.w);
                case Color c: return Floats(c.r, c.g, c.b, c.a);
                case Color32 c: { Color f = c; return Floats(f.r, f.g, f.b, f.a); }
                case Vector2Int v: return Floats(v.x, v.y);
                case Vector3Int v: return Floats(v.x, v.y, v.z);
                case Rect r: return Floats(r.x, r.y, r.width, r.height);
                case Bounds b: return Floats(b.center.x, b.center.y, b.center.z, b.size.x, b.size.y, b.size.z);
                case LayerMask m: return JsonValue.Create(m.value);
            }
            if (typeof(Object).IsAssignableFrom(t))
            {
                var o = value as Object;
                GameObject go = o switch { GameObject g => g, Component c => c.gameObject, _ => null };
                if (!o || go is null)
                    return null;
                return new JsonObject { ["entity"] = go.m_Id };
            }
            if (ElementType(t) is Type element)
            {
                var array = new JsonArray();
                if (value is IEnumerable items)
                    foreach (object item in items)
                        array.Add(Write(element, item, depth + 1));
                return array;
            }
            if (IsSerializableStruct(t))
            {
                var obj = new JsonObject();
                if (value == null)
                    value = CreateDefault(t);
                if (value != null)
                    foreach (FieldInfo f in SerializedFields(t))
                        obj[f.Name] = Write(f.FieldType, f.GetValue(value), depth + 1);
                return obj;
            }
            return null;
        }

        // ================================================================ read

        /// <summary>Applies JSON field values to an instance. Unknown or mistyped fields are skipped.</summary>
        public static void ReadInto(object instance, JsonObject values, Func<uint, uint> remap)
        {
            if (instance == null || values == null)
                return;
            foreach (FieldInfo f in SerializedFields(instance.GetType()))
            {
                JsonNode node = values[f.Name];
                if (node == null && !values.ContainsKey(f.Name))
                {
                    // Renamed field: [FormerlySerializedAs("old")]
                    foreach (var old in f.GetCustomAttributes<UnityEngine.Serialization.FormerlySerializedAsAttribute>())
                        if (values.TryGetPropertyValue(old.oldName, out node))
                            break;
                    if (node == null)
                        continue;
                }
                try
                {
                    f.SetValue(instance, Read(f.FieldType, node, f.GetValue(instance), remap, 0));
                }
                catch (Exception e)
                {
                    Debug.LogWarning($"Field '{instance.GetType().Name}.{f.Name}' could not be restored: {e.Message}");
                }
            }
        }

        /// <summary>
        /// Any JSON number as double. Values built in memory keep their CLR type (long, float, uint),
        /// which GetValue&lt;double&gt; refuses, so each numeric type is tried.
        /// </summary>
        internal static double Number(JsonNode n, double fallback = 0.0)
        {
            if (!(n is JsonValue v))
                return fallback;
            if (v.TryGetValue(out double d)) return d;
            if (v.TryGetValue(out float f)) return f;
            if (v.TryGetValue(out long l)) return l;
            if (v.TryGetValue(out int i)) return i;
            if (v.TryGetValue(out uint u)) return u;
            if (v.TryGetValue(out ulong ul)) return ul;
            if (v.TryGetValue(out decimal m)) return (double)m;
            if (v.TryGetValue(out bool b)) return b ? 1.0 : 0.0;
            if (v.TryGetValue(out string s) && double.TryParse(s, System.Globalization.NumberStyles.Float,
                                                                 System.Globalization.CultureInfo.InvariantCulture, out double parsed))
                return parsed;
            return fallback;
        }

        private static float F(JsonNode n, int i, float fallback = 0f)
        {
            if (n is JsonArray a && i < a.Count && a[i] != null)
                return (float)Number(a[i], fallback);
            return fallback;
        }

        private static object CreateDefault(Type t)
        {
            if (t.IsValueType)
                return Activator.CreateInstance(t);
            try
            {
                return Activator.CreateInstance(t, true);
            }
            catch
            {
                return null;
            }
        }

        public static object Read(Type t, JsonNode node, object existing, Func<uint, uint> remap, int depth)
        {
            if (depth > MaxDepth)
                return existing;
            if (t == typeof(string))
                return node?.GetValue<string>() ?? string.Empty;
            if (t == typeof(bool))
                return node != null && (node.GetValueKind() == System.Text.Json.JsonValueKind.True ||
                                        (node.GetValueKind() == System.Text.Json.JsonValueKind.Number && Number(node) != 0));
            if (t.IsEnum)
            {
                if (node == null) return existing;
                if (node.GetValueKind() == System.Text.Json.JsonValueKind.String)
                    return Enum.TryParse(t, node.GetValue<string>(), true, out object parsed) ? parsed : existing;
                return Enum.ToObject(t, (long)Number(node));
            }
            if (t == typeof(char))
            {
                string s = node?.GetValue<string>();
                return string.IsNullOrEmpty(s) ? '\0' : s[0];
            }
            if (t.IsPrimitive || t == typeof(decimal))
                return node == null ? existing : Convert.ChangeType(Number(node), t, System.Globalization.CultureInfo.InvariantCulture);
            if (t == typeof(Vector2)) return new Vector2(F(node, 0), F(node, 1));
            if (t == typeof(Vector3)) return new Vector3(F(node, 0), F(node, 1), F(node, 2));
            if (t == typeof(Vector4)) return new Vector4(F(node, 0), F(node, 1), F(node, 2), F(node, 3));
            if (t == typeof(Quaternion)) return new Quaternion(F(node, 0), F(node, 1), F(node, 2), F(node, 3, 1f)).normalized;
            if (t == typeof(Color)) return new Color(F(node, 0), F(node, 1), F(node, 2), F(node, 3, 1f));
            if (t == typeof(Color32)) return (Color32)new Color(F(node, 0), F(node, 1), F(node, 2), F(node, 3, 1f));
            if (t == typeof(Vector2Int)) return new Vector2Int((int)F(node, 0), (int)F(node, 1));
            if (t == typeof(Vector3Int)) return new Vector3Int((int)F(node, 0), (int)F(node, 1), (int)F(node, 2));
            if (t == typeof(Rect)) return new Rect(F(node, 0), F(node, 1), F(node, 2), F(node, 3));
            if (t == typeof(Bounds)) return new Bounds(new Vector3(F(node, 0), F(node, 1), F(node, 2)), new Vector3(F(node, 3), F(node, 4), F(node, 5)));
            if (t == typeof(LayerMask)) return (LayerMask)(node == null ? 0 : (int)Number(node));

            if (typeof(Object).IsAssignableFrom(t))
            {
                uint id = node is JsonObject reference ? (uint)Number(reference["entity"]) : 0u;
                if (id == 0)
                    return null;
                if (remap != null)
                    id = remap(id);
                GameObject go = World.GetGameObject(id);
                if (go == null)
                    return null;
                if (t == typeof(GameObject) || t == typeof(Object))
                    return go;
                return World.GetComponent(go, t);
            }

            if (ElementType(t) is Type element)
            {
                var items = new List<object>();
                if (node is JsonArray array)
                    foreach (JsonNode item in array)
                        items.Add(Read(element, item, CreateDefaultElement(element), remap, depth + 1));
                if (t.IsArray)
                {
                    Array result = Array.CreateInstance(element, items.Count);
                    for (int i = 0; i < items.Count; i++)
                        result.SetValue(items[i], i);
                    return result;
                }
                var list = (IList)Activator.CreateInstance(t);
                foreach (object item in items)
                    list.Add(item);
                return list;
            }

            if (IsSerializableStruct(t))
            {
                object target = existing ?? CreateDefault(t);
                if (target == null || !(node is JsonObject obj))
                    return target;
                foreach (FieldInfo f in SerializedFields(t))
                {
                    if (!obj.TryGetPropertyValue(f.Name, out JsonNode child))
                        continue;
                    f.SetValue(target, Read(f.FieldType, child, f.GetValue(target), remap, depth + 1));
                }
                return target;
            }
            return existing;
        }

        private static object CreateDefaultElement(Type element)
        {
            if (element.IsValueType)
                return Activator.CreateInstance(element);
            if (IsSerializableStruct(element))
                return CreateDefault(element);
            return null;
        }

        // ================================================================ inspector metadata

        public static string NicifyName(string name)
        {
            if (name.StartsWith("m_", StringComparison.Ordinal) && name.Length > 2)
                name = name.Substring(2);
            else if (name.StartsWith("_", StringComparison.Ordinal) && name.Length > 1)
                name = name.Substring(1);
            if (name.Length > 1 && name[0] == 'k' && char.IsUpper(name[1]))
                name = name.Substring(1);
            var sb = new StringBuilder();
            for (int i = 0; i < name.Length; i++)
            {
                char c = name[i];
                if (i == 0)
                {
                    sb.Append(char.ToUpperInvariant(c));
                    continue;
                }
                char prev = name[i - 1];
                bool boundary = (char.IsUpper(c) && (char.IsLower(prev) || (i + 1 < name.Length && char.IsLower(name[i + 1]) && char.IsUpper(prev)))) ||
                                (char.IsDigit(c) && !char.IsDigit(prev));
                if (boundary && prev != ' ')
                    sb.Append(' ');
                sb.Append(c == '_' ? ' ' : c);
            }
            return sb.ToString();
        }

        private static string Kind(Type t)
        {
            if (t == typeof(bool)) return "bool";
            if (t == typeof(string) || t == typeof(char)) return "string";
            if (t.IsEnum) return "enum";
            if (t == typeof(float) || t == typeof(double) || t == typeof(decimal)) return "float";
            if (t.IsPrimitive) return "int";
            if (t == typeof(Vector2)) return "vector2";
            if (t == typeof(Vector3)) return "vector3";
            if (t == typeof(Vector4)) return "vector4";
            if (t == typeof(Quaternion)) return "quaternion";
            if (t == typeof(Color) || t == typeof(Color32)) return "color";
            if (t == typeof(Vector2Int)) return "vector2int";
            if (t == typeof(Vector3Int)) return "vector3int";
            if (t == typeof(Rect)) return "rect";
            if (t == typeof(Bounds)) return "bounds";
            if (t == typeof(LayerMask)) return "layermask";
            if (typeof(Object).IsAssignableFrom(t)) return "object";
            if (ElementType(t) != null) return "list";
            return "struct";
        }

        public static JsonObject Describe(Type t, int depth)
        {
            var meta = new JsonObject { ["kind"] = Kind(t), ["type"] = t.Name };
            if (t.IsEnum)
            {
                var names = new JsonArray();
                var values = new JsonArray();
                foreach (object v in Enum.GetValues(t))
                {
                    names.Add(NicifyName(Enum.GetName(t, v)));
                    values.Add(Convert.ToInt64(v));
                }
                meta["enumNames"] = names;
                meta["enumValues"] = values;
                if (t.GetCustomAttribute<FlagsAttribute>() != null)
                    meta["flags"] = true;
            }
            else if (typeof(Object).IsAssignableFrom(t))
            {
                meta["sceneReference"] = t == typeof(GameObject) || typeof(Component).IsAssignableFrom(t);
            }
            else if (ElementType(t) is Type element)
            {
                meta["element"] = Describe(element, depth + 1);
            }
            else if (Kind(t) == "struct" && depth < MaxDepth)
            {
                meta["fields"] = DescribeFields(t, depth + 1);
            }
            return meta;
        }

        public static JsonArray DescribeFields(Type type, int depth)
        {
            var list = new JsonArray();
            object defaults = null;
            if (!type.IsAbstract)
            {
                try
                {
                    defaults = type.IsValueType ? Activator.CreateInstance(type) : Activator.CreateInstance(type, true);
                }
                catch
                {
                    defaults = null;
                }
            }
            foreach (FieldInfo f in SerializedFields(type))
            {
                if (f.GetCustomAttribute<HideInInspector>() != null)
                    continue;
                JsonObject meta = Describe(f.FieldType, depth);
                meta["name"] = f.Name;
                meta["label"] = NicifyName(f.Name);
                if (f.GetCustomAttribute<TooltipAttribute>() is TooltipAttribute tip)
                    meta["tooltip"] = tip.tooltip;
                var headers = new JsonArray();
                foreach (HeaderAttribute h in f.GetCustomAttributes<HeaderAttribute>())
                    headers.Add(h.header);
                if (headers.Count > 0)
                    meta["headers"] = headers;
                if (f.GetCustomAttribute<SpaceAttribute>() != null)
                    meta["space"] = true;
                if (f.GetCustomAttribute<RangeAttribute>() is RangeAttribute range)
                    meta["range"] = new JsonArray(range.min, range.max);
                if (f.GetCustomAttribute<MinAttribute>() is MinAttribute min)
                    meta["min"] = min.min;
                if (f.GetCustomAttribute<TextAreaAttribute>() != null || f.GetCustomAttribute<MultilineAttribute>() != null)
                    meta["multiline"] = true;
                if (defaults != null)
                {
                    try
                    {
                        meta["default"] = Write(f.FieldType, f.GetValue(defaults), depth);
                    }
                    catch
                    {
                        // A default that cannot be written just shows as empty.
                    }
                }
                list.Add(meta);
            }
            if (defaults is MonoBehaviour mb)
                mb.m_Destroyed = true;
            return list;
        }
    }
}
