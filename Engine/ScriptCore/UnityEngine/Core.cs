using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Text.Json;
using IndeetsEngine.Interop;
using IndeetsEngine.Runtime;

namespace UnityEngine
{
    public static class Time
    {
        internal static readonly Stopwatch s_Clock = Stopwatch.StartNew();

        public static float time { get; internal set; }
        public static double timeAsDouble => time;
        public static float deltaTime { get; internal set; }
        public static float unscaledTime { get; internal set; }
        public static double unscaledTimeAsDouble => unscaledTime;
        public static float unscaledDeltaTime { get; internal set; }
        public static float smoothDeltaTime { get; internal set; }
        public static float fixedTime { get; internal set; }
        public static float fixedUnscaledTime { get; internal set; }
        public static float fixedDeltaTime { get; set; } = 1f / 60f;
        public static float fixedUnscaledDeltaTime => fixedDeltaTime;
        public static float maximumDeltaTime { get; set; } = 1f / 3f;
        public static float timeScale { get; set; } = 1f;
        public static int frameCount { get; internal set; }
        public static bool inFixedTimeStep { get; internal set; }
        public static float timeSinceLevelLoad => time;
        public static float realtimeSinceStartup => (float)s_Clock.Elapsed.TotalSeconds;
        public static double realtimeSinceStartupAsDouble => s_Clock.Elapsed.TotalSeconds;
    }

    public enum LogType { Error = 0, Assert = 1, Warning = 2, Log = 3, Exception = 4 }

    public static class Debug
    {
        public static bool isDebugBuild => true;

        private static string Format(object message, Object context)
        {
            string text = message switch
            {
                null => "Null",
                IFormattable f => f.ToString(null, CultureInfo.InvariantCulture),
                _ => message.ToString(),
            };
            return text;
        }

        public static void Log(object message) => Native.Log(0, Format(message, null));
        public static void Log(object message, Object context) => Native.Log(0, Format(message, context));
        public static void LogFormat(string format, params object[] args) => Native.Log(0, string.Format(CultureInfo.InvariantCulture, format, args));
        public static void LogFormat(Object context, string format, params object[] args) => LogFormat(format, args);
        public static void LogWarning(object message) => Native.Log(1, Format(message, null));
        public static void LogWarning(object message, Object context) => Native.Log(1, Format(message, context));
        public static void LogWarningFormat(string format, params object[] args) => Native.Log(1, string.Format(CultureInfo.InvariantCulture, format, args));
        public static void LogWarningFormat(Object context, string format, params object[] args) => LogWarningFormat(format, args);
        public static void LogError(object message) => Native.Log(2, Format(message, null));
        public static void LogError(object message, Object context) => Native.Log(2, Format(message, context));
        public static void LogErrorFormat(string format, params object[] args) => Native.Log(2, string.Format(CultureInfo.InvariantCulture, format, args));
        public static void LogErrorFormat(Object context, string format, params object[] args) => LogErrorFormat(format, args);

        public static void LogException(Exception exception) => Native.Log(2, IndeetsEngine.Runtime.World.DescribeException(exception));
        public static void LogException(Exception exception, Object context) => LogException(exception);

        public static void Assert(bool condition) { if (!condition) Native.Log(2, "Assertion failed"); }
        public static void Assert(bool condition, object message) { if (!condition) Native.Log(2, "Assertion failed: " + Format(message, null)); }
        public static void Assert(bool condition, object message, Object context) => Assert(condition, message);
        public static void AssertFormat(bool condition, string format, params object[] args) { if (!condition) LogErrorFormat(format, args); }

        public static void DrawLine(Vector3 start, Vector3 end) { }
        public static void DrawLine(Vector3 start, Vector3 end, Color color, float duration = 0f, bool depthTest = true) { }
        public static void DrawRay(Vector3 start, Vector3 dir) { }
        public static void DrawRay(Vector3 start, Vector3 dir, Color color, float duration = 0f, bool depthTest = true) { }
        public static void Break() { }
    }

    public enum RuntimePlatform { WindowsPlayer = 2, WindowsEditor = 7, LinuxPlayer = 13, LinuxEditor = 16, OSXPlayer = 1, OSXEditor = 0 }

    public static class Application
    {
        public static bool isPlaying => IndeetsEngine.Runtime.World.IsPlaying;
        public static bool isEditor { get; internal set; }
        public static bool isFocused => true;
        public static bool isBatchMode => false;
        public static bool runInBackground { get; set; } = true;
        public static int targetFrameRate { get; set; } = -1;
        public static string productName { get; internal set; } = "IndeetsEngine Game";
        public static string companyName { get; internal set; } = "DefaultCompany";
        public static string version { get; internal set; } = "0.1";
        public static string unityVersion => "6000.0.0f1";
        public static string identifier => companyName + "." + productName;
        public static SystemLanguage systemLanguage =>
            CultureInfo.CurrentUICulture.TwoLetterISOLanguageName == "ru" ? SystemLanguage.Russian : SystemLanguage.English;

        public static RuntimePlatform platform =>
            OperatingSystem.IsWindows() ? (isEditor ? RuntimePlatform.WindowsEditor : RuntimePlatform.WindowsPlayer)
            : OperatingSystem.IsMacOS() ? (isEditor ? RuntimePlatform.OSXEditor : RuntimePlatform.OSXPlayer)
            : isEditor ? RuntimePlatform.LinuxEditor : RuntimePlatform.LinuxPlayer;

        public static string dataPath { get; internal set; } = Directory.GetCurrentDirectory();
        public static string streamingAssetsPath => Path.Combine(dataPath, "StreamingAssets");

        public static string persistentDataPath
        {
            get
            {
                string root = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
                if (OperatingSystem.IsWindows())
                    root = Path.Combine(Path.GetDirectoryName(root) ?? root, "LocalLow");
                string path = Path.Combine(root, companyName, productName);
                Directory.CreateDirectory(path);
                return path;
            }
        }

        public static string temporaryCachePath => Path.Combine(Path.GetTempPath(), companyName, productName);

        public static event Action quitting;
        public static event Action<bool> focusChanged;

        internal static void RaiseQuitting()
        {
            quitting?.Invoke();
            _ = focusChanged;
        }

        public static void Quit() => Quit(0);
        public static void Quit(int exitCode) => IndeetsEngine.Runtime.World.RequestQuit();
        public static void OpenURL(string url)
        {
            try { Process.Start(new ProcessStartInfo(url) { UseShellExecute = true }); }
            catch (Exception e) { Debug.LogWarning($"OpenURL failed: {e.Message}"); }
        }
    }

    public enum SystemLanguage { English = 10, Russian = 30, Unknown = 42 }

    public struct Resolution
    {
        public int width { get; set; }
        public int height { get; set; }
        public override string ToString() => $"{width} x {height}";
    }

    public enum FullScreenMode { ExclusiveFullScreen = 0, FullScreenWindow = 1, MaximizedWindow = 2, Windowed = 3 }

    public static class Screen
    {
        private static unsafe Vector2 Size
        {
            get
            {
                if (!Native.IsAvailable)
                    return new Vector2(1920f, 1080f);
                float* s = stackalloc float[2];
                Native.Api.ScreenSize(s);
                return new Vector2(s[0], s[1]);
            }
        }

        public static int width => (int)Size.x;
        public static int height => (int)Size.y;
        public static float dpi => 96f;
        public static bool fullScreen { get; set; }
        public static FullScreenMode fullScreenMode { get; set; } = FullScreenMode.Windowed;
        public static Resolution currentResolution => new Resolution { width = width, height = height };
        public static Resolution[] resolutions => new[] { currentResolution };
        public static Rect safeArea => new Rect(0f, 0f, width, height);
        public static void SetResolution(int width, int height, bool fullscreen) { }
        public static void SetResolution(int width, int height, FullScreenMode mode) { }
    }

    public enum CursorLockMode { None = 0, Locked = 1, Confined = 2 }

    public static class Cursor
    {
        private static CursorLockMode s_LockState;

        public static bool visible { get; set; } = true;

        public static CursorLockMode lockState
        {
            get => s_LockState;
            set
            {
                s_LockState = value;
                if (Native.IsAvailable)
                    unsafe { Native.Api.InputSetCursorLocked(value == CursorLockMode.Locked ? 1 : 0); }
            }
        }

        internal static void Reset()
        {
            s_LockState = CursorLockMode.None;
            visible = true;
        }
    }

    /// <summary>Unity's PlayerPrefs, stored as JSON in <see cref="Application.persistentDataPath"/>.</summary>
    public static class PlayerPrefs
    {
        private static Dictionary<string, JsonElement> s_Values;
        private static readonly Dictionary<string, object> s_Pending = new Dictionary<string, object>();

        private static string FilePath => Path.Combine(Application.persistentDataPath, "PlayerPrefs.json");

        private static Dictionary<string, object> Values
        {
            get
            {
                if (s_Values == null)
                {
                    s_Values = new Dictionary<string, JsonElement>();
                    try
                    {
                        if (File.Exists(FilePath))
                            s_Values = JsonSerializer.Deserialize<Dictionary<string, JsonElement>>(File.ReadAllText(FilePath))
                                       ?? new Dictionary<string, JsonElement>();
                    }
                    catch (Exception e)
                    {
                        Debug.LogWarning($"PlayerPrefs could not be read: {e.Message}");
                    }
                    foreach (var kv in s_Values)
                        s_Pending[kv.Key] = kv.Value.ValueKind switch
                        {
                            JsonValueKind.Number when kv.Value.TryGetInt32(out int i) => i,
                            JsonValueKind.Number => kv.Value.GetSingle(),
                            JsonValueKind.String => kv.Value.GetString(),
                            _ => null,
                        };
                }
                return s_Pending;
            }
        }

        public static void SetInt(string key, int value) => Values[key] = value;
        public static void SetFloat(string key, float value) => Values[key] = value;
        public static void SetString(string key, string value) => Values[key] = value ?? string.Empty;

        public static int GetInt(string key, int defaultValue = 0) =>
            Values.TryGetValue(key, out object v) ? v switch { int i => i, float f => (int)f, _ => defaultValue } : defaultValue;

        public static float GetFloat(string key, float defaultValue = 0f) =>
            Values.TryGetValue(key, out object v) ? v switch { float f => f, int i => i, _ => defaultValue } : defaultValue;

        public static string GetString(string key, string defaultValue = "") =>
            Values.TryGetValue(key, out object v) && v is string s ? s : defaultValue;

        public static bool HasKey(string key) => Values.ContainsKey(key);
        public static void DeleteKey(string key) => Values.Remove(key);
        public static void DeleteAll() => Values.Clear();

        public static void Save()
        {
            try
            {
                File.WriteAllText(FilePath, JsonSerializer.Serialize(Values, new JsonSerializerOptions { WriteIndented = true }));
            }
            catch (Exception e)
            {
                Debug.LogWarning($"PlayerPrefs could not be saved: {e.Message}");
            }
        }
    }

    /// <summary>
    /// Assets inside "Resources" folders, by their path below the folder without extension
    /// ("Folder/Name"). Each asset is loaded once and shared, like Unity.
    /// </summary>
    public static class Resources
    {
        public static T Load<T>(string path) where T : Object => (T)AssetLoader.ResourcesLoad(path, typeof(T));
        public static Object Load(string path) => AssetLoader.ResourcesLoad(path, null);
        public static Object Load(string path, Type systemTypeInstance) => AssetLoader.ResourcesLoad(path, systemTypeInstance);

        public static T[] LoadAll<T>(string path) where T : Object
        {
            List<Object> found = AssetLoader.ResourcesLoadAll(path, typeof(T));
            var result = new T[found.Count];
            for (int i = 0; i < found.Count; i++)
                result[i] = (T)found[i];
            return result;
        }

        public static Object[] LoadAll(string path) => AssetLoader.ResourcesLoadAll(path, null).ToArray();
        public static Object[] LoadAll(string path, Type systemTypeInstance) =>
            AssetLoader.ResourcesLoadAll(path, systemTypeInstance).ToArray();

        // Loading is synchronous: the request is done when returned.
        public static ResourceRequest LoadAsync<T>(string path) where T : Object => new ResourceRequest(Load<T>(path));
        public static ResourceRequest LoadAsync(string path) => new ResourceRequest(Load(path));
        public static ResourceRequest LoadAsync(string path, Type type) => new ResourceRequest(Load(path, type));

        public static T GetBuiltinResource<T>(string path) where T : Object => null;
        public static AsyncOperation UnloadUnusedAssets() => new AsyncOperation();
        public static void UnloadAsset(Object assetToUnload) { }
    }

    public class ResourceRequest : AsyncOperation
    {
        public ResourceRequest() { }
        internal ResourceRequest(Object asset) { this.asset = asset; }

        public Object asset { get; }
    }

    // ------------------------------------------------------------------ attributes

    [AttributeUsage(AttributeTargets.Field)] public sealed class SerializeField : Attribute { }
    [AttributeUsage(AttributeTargets.Field)] public sealed class SerializeReference : Attribute { }
    [AttributeUsage(AttributeTargets.Field)] public sealed class HideInInspector : Attribute { }
    [AttributeUsage(AttributeTargets.Field)] public sealed class NonReorderableAttribute : Attribute { }

    public abstract class PropertyAttribute : Attribute
    {
        public int order { get; set; }
    }

    [AttributeUsage(AttributeTargets.Field, AllowMultiple = true)]
    public sealed class HeaderAttribute : PropertyAttribute
    {
        public readonly string header;
        public HeaderAttribute(string header) { this.header = header; }
    }

    [AttributeUsage(AttributeTargets.Field | AttributeTargets.Class, AllowMultiple = true)]
    public sealed class SpaceAttribute : PropertyAttribute
    {
        public readonly float height;
        public SpaceAttribute() { height = 8f; }
        public SpaceAttribute(float height) { this.height = height; }
    }

    [AttributeUsage(AttributeTargets.Field)]
    public sealed class TooltipAttribute : PropertyAttribute
    {
        public readonly string tooltip;
        public TooltipAttribute(string tooltip) { this.tooltip = tooltip; }
    }

    [AttributeUsage(AttributeTargets.Field)]
    public sealed class RangeAttribute : PropertyAttribute
    {
        public readonly float min;
        public readonly float max;
        public RangeAttribute(float min, float max) { this.min = min; this.max = max; }
    }

    [AttributeUsage(AttributeTargets.Field)]
    public sealed class MinAttribute : PropertyAttribute
    {
        public readonly float min;
        public MinAttribute(float min) { this.min = min; }
    }

    [AttributeUsage(AttributeTargets.Field)]
    public sealed class TextAreaAttribute : PropertyAttribute
    {
        public readonly int minLines;
        public readonly int maxLines;
        public TextAreaAttribute() { minLines = 3; maxLines = 3; }
        public TextAreaAttribute(int minLines, int maxLines) { this.minLines = minLines; this.maxLines = maxLines; }
    }

    [AttributeUsage(AttributeTargets.Field)]
    public sealed class MultilineAttribute : PropertyAttribute
    {
        public MultilineAttribute() { }
        public MultilineAttribute(int lines) { }
    }

    [AttributeUsage(AttributeTargets.Field)]
    public sealed class ColorUsageAttribute : PropertyAttribute
    {
        public ColorUsageAttribute(bool showAlpha) { }
        public ColorUsageAttribute(bool showAlpha, bool hdr) { }
    }

    [AttributeUsage(AttributeTargets.Field)] public sealed class DelayedAttribute : PropertyAttribute { }
    [AttributeUsage(AttributeTargets.Field)] public sealed class InspectorNameAttribute : PropertyAttribute { public InspectorNameAttribute(string displayName) { } }

    [AttributeUsage(AttributeTargets.Field)]
    public sealed class ContextMenuItemAttribute : PropertyAttribute
    {
        public ContextMenuItemAttribute(string name, string function) { }
    }

    [AttributeUsage(AttributeTargets.Class, AllowMultiple = true)]
    public sealed class RequireComponent : Attribute
    {
        public Type m_Type0, m_Type1, m_Type2;
        public RequireComponent(Type requiredComponent) { m_Type0 = requiredComponent; }
        public RequireComponent(Type requiredComponent, Type requiredComponent2) : this(requiredComponent) { m_Type1 = requiredComponent2; }
        public RequireComponent(Type requiredComponent, Type requiredComponent2, Type requiredComponent3) : this(requiredComponent, requiredComponent2) { m_Type2 = requiredComponent3; }
    }

    [AttributeUsage(AttributeTargets.Class)] public sealed class DisallowMultipleComponent : Attribute { }
    [AttributeUsage(AttributeTargets.Class)] public sealed class ExecuteInEditMode : Attribute { }
    [AttributeUsage(AttributeTargets.Class)] public sealed class ExecuteAlways : Attribute { }
    [AttributeUsage(AttributeTargets.Class)] public sealed class SelectionBaseAttribute : Attribute { }
    [AttributeUsage(AttributeTargets.Class)] public sealed class HelpURLAttribute : Attribute { public HelpURLAttribute(string url) { } }
    [AttributeUsage(AttributeTargets.Class)] public sealed class IconAttribute : Attribute { public IconAttribute(string path) { } }

    [AttributeUsage(AttributeTargets.Class)]
    public sealed class AddComponentMenu : Attribute
    {
        public AddComponentMenu(string menuName) { }
        public AddComponentMenu(string menuName, int order) { }
    }

    [AttributeUsage(AttributeTargets.Class)]
    public sealed class CreateAssetMenuAttribute : Attribute
    {
        public string menuName { get; set; }
        public string fileName { get; set; }
        public int order { get; set; }
    }

    [AttributeUsage(AttributeTargets.Class)]
    public sealed class DefaultExecutionOrder : Attribute
    {
        public int order { get; }
        public DefaultExecutionOrder(int order) { this.order = order; }
    }

    [AttributeUsage(AttributeTargets.Method)]
    public sealed class ContextMenu : Attribute
    {
        public ContextMenu(string itemName) { }
        public ContextMenu(string itemName, bool isValidateFunction) { }
    }

    public enum RuntimeInitializeLoadType
    {
        AfterSceneLoad = 0,
        BeforeSceneLoad = 1,
        AfterAssembliesLoaded = 2,
        BeforeSplashScreen = 3,
        SubsystemRegistration = 4,
    }

    [AttributeUsage(AttributeTargets.Method)]
    public sealed class RuntimeInitializeOnLoadMethodAttribute : Attribute
    {
        public RuntimeInitializeLoadType loadType { get; }
        public RuntimeInitializeOnLoadMethodAttribute() { loadType = RuntimeInitializeLoadType.AfterSceneLoad; }
        public RuntimeInitializeOnLoadMethodAttribute(RuntimeInitializeLoadType loadType) { this.loadType = loadType; }
    }
}

namespace UnityEngine.Serialization
{
    [AttributeUsage(AttributeTargets.Field, AllowMultiple = true)]
    public sealed class FormerlySerializedAsAttribute : Attribute
    {
        public string oldName { get; }
        public FormerlySerializedAsAttribute(string oldName) { this.oldName = oldName; }
    }
}

namespace UnityEngine.Scripting
{
    [AttributeUsage(AttributeTargets.All)] public sealed class PreserveAttribute : Attribute { }
}
