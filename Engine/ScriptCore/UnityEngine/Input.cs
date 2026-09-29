using System;
using System.Collections.Generic;
using IndeetsEngine.Interop;

namespace UnityEngine
{
    /// <summary>Unity's KeyCode values.</summary>
    public enum KeyCode
    {
        None = 0, Backspace = 8, Tab = 9, Clear = 12, Return = 13, Pause = 19, Escape = 27, Space = 32,
        Exclaim = 33, DoubleQuote = 34, Hash = 35, Dollar = 36, Percent = 37, Ampersand = 38, Quote = 39,
        LeftParen = 40, RightParen = 41, Asterisk = 42, Plus = 43, Comma = 44, Minus = 45, Period = 46, Slash = 47,
        Alpha0 = 48, Alpha1, Alpha2, Alpha3, Alpha4, Alpha5, Alpha6, Alpha7, Alpha8, Alpha9,
        Colon = 58, Semicolon = 59, Less = 60, Equals = 61, Greater = 62, Question = 63, At = 64,
        LeftBracket = 91, Backslash = 92, RightBracket = 93, Caret = 94, Underscore = 95, BackQuote = 96,
        A = 97, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
        LeftCurlyBracket = 123, Pipe = 124, RightCurlyBracket = 125, Tilde = 126, Delete = 127,
        Keypad0 = 256, Keypad1, Keypad2, Keypad3, Keypad4, Keypad5, Keypad6, Keypad7, Keypad8, Keypad9,
        KeypadPeriod = 266, KeypadDivide = 267, KeypadMultiply = 268, KeypadMinus = 269, KeypadPlus = 270,
        KeypadEnter = 271, KeypadEquals = 272,
        UpArrow = 273, DownArrow = 274, RightArrow = 275, LeftArrow = 276, Insert = 277, Home = 278, End = 279,
        PageUp = 280, PageDown = 281,
        F1 = 282, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13, F14, F15,
        Numlock = 300, CapsLock = 301, ScrollLock = 302, RightShift = 303, LeftShift = 304,
        RightControl = 305, LeftControl = 306, RightAlt = 307, LeftAlt = 308,
        RightCommand = 309, RightApple = 309, LeftCommand = 310, LeftApple = 310, LeftWindows = 311, RightWindows = 312,
        AltGr = 313, Help = 315, Print = 316, SysReq = 317, Break = 318, Menu = 319,
        Mouse0 = 323, Mouse1, Mouse2, Mouse3, Mouse4, Mouse5, Mouse6,
    }

    public static class Input
    {
        // Engine key codes follow GLFW.
        private static readonly Dictionary<KeyCode, int> s_Map = BuildMap();

        private static Dictionary<KeyCode, int> BuildMap()
        {
            var m = new Dictionary<KeyCode, int>
            {
                [KeyCode.Space] = 32, [KeyCode.Quote] = 39, [KeyCode.Comma] = 44, [KeyCode.Minus] = 45,
                [KeyCode.Period] = 46, [KeyCode.Slash] = 47, [KeyCode.Semicolon] = 59, [KeyCode.Equals] = 61,
                [KeyCode.LeftBracket] = 91, [KeyCode.Backslash] = 92, [KeyCode.RightBracket] = 93, [KeyCode.BackQuote] = 96,
                [KeyCode.Escape] = 256, [KeyCode.Return] = 257, [KeyCode.Tab] = 258, [KeyCode.Backspace] = 259,
                [KeyCode.Insert] = 260, [KeyCode.Delete] = 261, [KeyCode.RightArrow] = 262, [KeyCode.LeftArrow] = 263,
                [KeyCode.DownArrow] = 264, [KeyCode.UpArrow] = 265, [KeyCode.PageUp] = 266, [KeyCode.PageDown] = 267,
                [KeyCode.Home] = 268, [KeyCode.End] = 269, [KeyCode.CapsLock] = 280, [KeyCode.ScrollLock] = 281,
                [KeyCode.Numlock] = 282, [KeyCode.Print] = 283, [KeyCode.Pause] = 284,
                [KeyCode.KeypadPeriod] = 330, [KeyCode.KeypadDivide] = 331, [KeyCode.KeypadMultiply] = 332,
                [KeyCode.KeypadMinus] = 333, [KeyCode.KeypadPlus] = 334, [KeyCode.KeypadEnter] = 335, [KeyCode.KeypadEquals] = 336,
                [KeyCode.LeftShift] = 340, [KeyCode.LeftControl] = 341, [KeyCode.LeftAlt] = 342, [KeyCode.LeftWindows] = 343,
                [KeyCode.RightShift] = 344, [KeyCode.RightControl] = 345, [KeyCode.RightAlt] = 346, [KeyCode.RightWindows] = 347,
                [KeyCode.Menu] = 348,
            };
            for (int i = 0; i < 26; i++)
                m[KeyCode.A + i] = 65 + i;
            for (int i = 0; i < 10; i++)
            {
                m[KeyCode.Alpha0 + i] = 48 + i;
                m[KeyCode.Keypad0 + i] = 320 + i;
            }
            for (int i = 0; i < 15; i++)
                m[KeyCode.F1 + i] = 290 + i;
            return m;
        }

        internal static int ToEngineKey(KeyCode key) => s_Map.TryGetValue(key, out int k) ? k : -1;

        private static bool Query(KeyCode key, int query)
        {
            if (!Native.IsAvailable)
                return false;
            if (key >= KeyCode.Mouse0 && key <= KeyCode.Mouse6)
                return MouseQuery(key - KeyCode.Mouse0, query);
            int k = ToEngineKey(key);
            unsafe { return k >= 0 && Native.Api.InputKey(k, query) != 0; }
        }

        internal static bool EngineKey(int engineKey, int query)
        {
            if (!Native.IsAvailable || engineKey < 0)
                return false;
            unsafe { return Native.Api.InputKey(engineKey, query) != 0; }
        }

        internal static bool MouseQuery(int button, int query)
        {
            if (!Native.IsAvailable)
                return false;
            unsafe { return Native.Api.InputMouseButton(button, query) != 0; }
        }

        internal static unsafe void MouseState(out Vector2 position, out Vector2 delta, out float scroll)
        {
            position = delta = Vector2.zero;
            scroll = 0f;
            if (!Native.IsAvailable)
                return;
            float* s = stackalloc float[5];
            Native.Api.InputMouseState(s);
            position = new Vector2(s[0], s[1]);
            delta = new Vector2(s[2], s[3]);
            scroll = s[4];
        }

        public static bool GetKey(KeyCode key) => Query(key, 0);
        public static bool GetKeyDown(KeyCode key) => Query(key, 1);
        public static bool GetKeyUp(KeyCode key) => Query(key, 2);
        public static bool GetKey(string name) => TryParseKey(name, out KeyCode k) && GetKey(k);
        public static bool GetKeyDown(string name) => TryParseKey(name, out KeyCode k) && GetKeyDown(k);
        public static bool GetKeyUp(string name) => TryParseKey(name, out KeyCode k) && GetKeyUp(k);

        private static bool TryParseKey(string name, out KeyCode key)
        {
            key = KeyCode.None;
            if (string.IsNullOrEmpty(name))
                return false;
            switch (name.ToLowerInvariant())
            {
                case "space": key = KeyCode.Space; return true;
                case "left shift": key = KeyCode.LeftShift; return true;
                case "right shift": key = KeyCode.RightShift; return true;
                case "left ctrl": key = KeyCode.LeftControl; return true;
                case "right ctrl": key = KeyCode.RightControl; return true;
                case "up": key = KeyCode.UpArrow; return true;
                case "down": key = KeyCode.DownArrow; return true;
                case "left": key = KeyCode.LeftArrow; return true;
                case "right": key = KeyCode.RightArrow; return true;
                case "escape": key = KeyCode.Escape; return true;
                case "return": case "enter": key = KeyCode.Return; return true;
            }
            if (name.Length == 1 && char.IsLetter(name[0]))
            {
                key = KeyCode.A + (char.ToLowerInvariant(name[0]) - 'a');
                return true;
            }
            if (name.Length == 1 && char.IsDigit(name[0]))
            {
                key = KeyCode.Alpha0 + (name[0] - '0');
                return true;
            }
            return Enum.TryParse(name, true, out key);
        }

        public static bool GetMouseButton(int button) => MouseQuery(button, 0);
        public static bool GetMouseButtonDown(int button) => MouseQuery(button, 1);
        public static bool GetMouseButtonUp(int button) => MouseQuery(button, 2);

        public static Vector3 mousePosition { get { MouseState(out Vector2 p, out _, out _); return p; } }
        public static Vector2 mouseScrollDelta { get { MouseState(out _, out _, out float s); return new Vector2(0f, s); } }
        public static bool mousePresent => true;

        public static bool anyKey
        {
            get
            {
                foreach (int k in s_Map.Values)
                    if (EngineKey(k, 0)) return true;
                return MouseQuery(0, 0) || MouseQuery(1, 0) || MouseQuery(2, 0);
            }
        }

        public static bool anyKeyDown
        {
            get
            {
                foreach (int k in s_Map.Values)
                    if (EngineKey(k, 1)) return true;
                return MouseQuery(0, 1) || MouseQuery(1, 1) || MouseQuery(2, 1);
            }
        }

        public static string inputString => string.Empty;

        /// <summary>The default Input Manager axes.</summary>
        public static float GetAxisRaw(string axisName)
        {
            switch (axisName)
            {
                case "Horizontal":
                    return (GetKey(KeyCode.D) || GetKey(KeyCode.RightArrow) ? 1f : 0f) - (GetKey(KeyCode.A) || GetKey(KeyCode.LeftArrow) ? 1f : 0f);
                case "Vertical":
                    return (GetKey(KeyCode.W) || GetKey(KeyCode.UpArrow) ? 1f : 0f) - (GetKey(KeyCode.S) || GetKey(KeyCode.DownArrow) ? 1f : 0f);
                case "Mouse X": { MouseState(out _, out Vector2 d, out _); return d.x * 0.1f; }
                case "Mouse Y": { MouseState(out _, out Vector2 d, out _); return d.y * 0.1f; }
                case "Mouse ScrollWheel": { MouseState(out _, out _, out float s); return s * 0.1f; }
                case "Jump": return GetKey(KeyCode.Space) ? 1f : 0f;
                case "Fire1": return GetKey(KeyCode.LeftControl) || GetMouseButton(0) ? 1f : 0f;
                case "Fire2": return GetKey(KeyCode.LeftAlt) || GetMouseButton(1) ? 1f : 0f;
                case "Fire3": return GetKey(KeyCode.LeftShift) || GetMouseButton(2) ? 1f : 0f;
                case "Submit": return GetKey(KeyCode.Return) || GetKey(KeyCode.KeypadEnter) ? 1f : 0f;
                case "Cancel": return GetKey(KeyCode.Escape) ? 1f : 0f;
                default:
                    throw new ArgumentException($"Input Axis {axisName} is not setup.");
            }
        }

        private static readonly Dictionary<string, float> s_Smoothed = new Dictionary<string, float>();

        /// <summary>Like Unity's keyboard axes: moves towards the raw value at 3 units/s, snaps on reversal.</summary>
        public static float GetAxis(string axisName)
        {
            float raw = GetAxisRaw(axisName);
            if (axisName.StartsWith("Mouse", StringComparison.Ordinal))
                return raw;
            s_Smoothed.TryGetValue(axisName, out float value);
            if (raw != 0f && MathF.Sign(raw) != MathF.Sign(value))
                value = 0f;
            const float SensitivityAndGravity = 3f;
            value = Mathf.MoveTowards(value, raw, SensitivityAndGravity * Time.unscaledDeltaTime);
            s_Smoothed[axisName] = value;
            return value;
        }

        public static bool GetButton(string buttonName) => GetAxisRaw(buttonName) != 0f;
        public static bool GetButtonDown(string buttonName) => buttonName switch
        {
            "Jump" => GetKeyDown(KeyCode.Space),
            "Fire1" => GetKeyDown(KeyCode.LeftControl) || GetMouseButtonDown(0),
            "Fire2" => GetKeyDown(KeyCode.LeftAlt) || GetMouseButtonDown(1),
            "Fire3" => GetKeyDown(KeyCode.LeftShift) || GetMouseButtonDown(2),
            "Submit" => GetKeyDown(KeyCode.Return) || GetKeyDown(KeyCode.KeypadEnter),
            "Cancel" => GetKeyDown(KeyCode.Escape),
            _ => false,
        };

        public static bool GetButtonUp(string buttonName) => buttonName switch
        {
            "Jump" => GetKeyUp(KeyCode.Space),
            "Fire1" => GetKeyUp(KeyCode.LeftControl) || GetMouseButtonUp(0),
            "Fire2" => GetKeyUp(KeyCode.LeftAlt) || GetMouseButtonUp(1),
            "Fire3" => GetKeyUp(KeyCode.LeftShift) || GetMouseButtonUp(2),
            "Submit" => GetKeyUp(KeyCode.Return) || GetKeyUp(KeyCode.KeypadEnter),
            "Cancel" => GetKeyUp(KeyCode.Escape),
            _ => false,
        };

        public static void ResetInputAxes() => s_Smoothed.Clear();

        internal static void ResetState() => s_Smoothed.Clear();
    }
}

namespace UnityEngine.InputSystem
{
    /// <summary>Key identifiers of the Input System package (same values).</summary>
    public enum Key
    {
        None = 0, Space = 1, Enter = 2, Tab = 3, Backquote = 4, Quote = 5, Semicolon = 6, Comma = 7, Period = 8,
        Slash = 9, Backslash = 10, LeftBracket = 11, RightBracket = 12, Minus = 13, Equals = 14,
        A = 15, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
        Digit1 = 41, Digit2, Digit3, Digit4, Digit5, Digit6, Digit7, Digit8, Digit9, Digit0 = 50,
        LeftShift = 51, RightShift = 52, LeftAlt = 53, RightAlt = 54, AltGr = 54, LeftCtrl = 55, RightCtrl = 56,
        LeftMeta = 57, RightMeta = 58, LeftWindows = 57, RightWindows = 58, LeftApple = 57, RightApple = 58,
        LeftCommand = 57, RightCommand = 58, ContextMenu = 59, Escape = 60,
        LeftArrow = 61, RightArrow = 62, UpArrow = 63, DownArrow = 64, Backspace = 65, PageDown = 66, PageUp = 67,
        Home = 68, End = 69, Insert = 70, Delete = 71, CapsLock = 72, NumLock = 73, PrintScreen = 74, ScrollLock = 75,
        Pause = 76, NumpadEnter = 77, NumpadDivide = 78, NumpadMultiply = 79, NumpadPlus = 80, NumpadMinus = 81,
        NumpadPeriod = 82, NumpadEquals = 83,
        Numpad0 = 84, Numpad1, Numpad2, Numpad3, Numpad4, Numpad5, Numpad6, Numpad7, Numpad8, Numpad9,
        F1 = 94, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    }

    public abstract class InputControl
    {
        public string name { get; internal set; }
        public string displayName => name;
        public string path => "/" + name;
    }

    public abstract class InputControl<TValue> : InputControl where TValue : struct
    {
        public abstract TValue ReadValue();
        public TValue value => ReadValue();
    }

    public class ButtonControl : InputControl<float>
    {
        internal Func<int, bool> m_Query;

        public bool isPressed => m_Query(0);
        public bool wasPressedThisFrame => m_Query(1);
        public bool wasReleasedThisFrame => m_Query(2);
        public override float ReadValue() => isPressed ? 1f : 0f;
    }

    public sealed class KeyControl : ButtonControl
    {
        public Key keyCode { get; internal set; }
    }

    public sealed class Vector2Control : InputControl<Vector2>
    {
        internal Func<Vector2> m_Read;
        public override Vector2 ReadValue() => m_Read();
        public AxisControl x => new AxisControl { m_Read = () => m_Read().x, name = "x" };
        public AxisControl y => new AxisControl { m_Read = () => m_Read().y, name = "y" };
    }

    public sealed class AxisControl : InputControl<float>
    {
        internal Func<float> m_Read;
        public override float ReadValue() => m_Read();
    }

    public abstract class InputDevice : InputControl
    {
        public bool added => true;
        public bool enabled => true;
    }

    public sealed class Keyboard : InputDevice
    {
        private static Keyboard s_Current;
        private readonly Dictionary<Key, KeyControl> m_Keys = new Dictionary<Key, KeyControl>();

        public static Keyboard current => s_Current ??= new Keyboard { name = "Keyboard" };

        internal static int ToEngineKey(Key key)
        {
            if (key >= Key.A && key <= Key.Z) return 65 + (key - Key.A);
            if (key >= Key.Digit1 && key <= Key.Digit9) return 49 + (key - Key.Digit1);
            if (key >= Key.Numpad0 && key <= Key.Numpad9) return 320 + (key - Key.Numpad0);
            if (key >= Key.F1 && key <= Key.F12) return 290 + (key - Key.F1);
            return key switch
            {
                Key.Digit0 => 48, Key.Space => 32, Key.Enter => 257, Key.Tab => 258, Key.Backquote => 96, Key.Quote => 39,
                Key.Semicolon => 59, Key.Comma => 44, Key.Period => 46, Key.Slash => 47, Key.Backslash => 92,
                Key.LeftBracket => 91, Key.RightBracket => 93, Key.Minus => 45, Key.Equals => 61,
                Key.LeftShift => 340, Key.RightShift => 344, Key.LeftAlt => 342, Key.RightAlt => 346,
                Key.LeftCtrl => 341, Key.RightCtrl => 345, Key.LeftMeta => 343, Key.RightMeta => 347, Key.ContextMenu => 348,
                Key.Escape => 256, Key.LeftArrow => 263, Key.RightArrow => 262, Key.UpArrow => 265, Key.DownArrow => 264,
                Key.Backspace => 259, Key.PageDown => 267, Key.PageUp => 266, Key.Home => 268, Key.End => 269,
                Key.Insert => 260, Key.Delete => 261, Key.CapsLock => 280, Key.NumLock => 282, Key.PrintScreen => 283,
                Key.ScrollLock => 281, Key.Pause => 284, Key.NumpadEnter => 335, Key.NumpadDivide => 331,
                Key.NumpadMultiply => 332, Key.NumpadPlus => 334, Key.NumpadMinus => 333, Key.NumpadPeriod => 330,
                Key.NumpadEquals => 336,
                _ => -1,
            };
        }

        public KeyControl this[Key key]
        {
            get
            {
                if (!m_Keys.TryGetValue(key, out KeyControl control))
                {
                    int engineKey = ToEngineKey(key);
                    control = new KeyControl
                    {
                        name = key.ToString(),
                        keyCode = key,
                        m_Query = q => Input.EngineKey(engineKey, q),
                    };
                    m_Keys[key] = control;
                }
                return control;
            }
        }

        public ButtonControl anyKey => new ButtonControl { name = "anyKey", m_Query = q => q == 1 ? Input.anyKeyDown : Input.anyKey };

        public KeyControl spaceKey => this[Key.Space];
        public KeyControl enterKey => this[Key.Enter];
        public KeyControl tabKey => this[Key.Tab];
        public KeyControl backquoteKey => this[Key.Backquote];
        public KeyControl quoteKey => this[Key.Quote];
        public KeyControl semicolonKey => this[Key.Semicolon];
        public KeyControl commaKey => this[Key.Comma];
        public KeyControl periodKey => this[Key.Period];
        public KeyControl slashKey => this[Key.Slash];
        public KeyControl backslashKey => this[Key.Backslash];
        public KeyControl leftBracketKey => this[Key.LeftBracket];
        public KeyControl rightBracketKey => this[Key.RightBracket];
        public KeyControl minusKey => this[Key.Minus];
        public KeyControl equalsKey => this[Key.Equals];
        public KeyControl aKey => this[Key.A];
        public KeyControl bKey => this[Key.B];
        public KeyControl cKey => this[Key.C];
        public KeyControl dKey => this[Key.D];
        public KeyControl eKey => this[Key.E];
        public KeyControl fKey => this[Key.F];
        public KeyControl gKey => this[Key.G];
        public KeyControl hKey => this[Key.H];
        public KeyControl iKey => this[Key.I];
        public KeyControl jKey => this[Key.J];
        public KeyControl kKey => this[Key.K];
        public KeyControl lKey => this[Key.L];
        public KeyControl mKey => this[Key.M];
        public KeyControl nKey => this[Key.N];
        public KeyControl oKey => this[Key.O];
        public KeyControl pKey => this[Key.P];
        public KeyControl qKey => this[Key.Q];
        public KeyControl rKey => this[Key.R];
        public KeyControl sKey => this[Key.S];
        public KeyControl tKey => this[Key.T];
        public KeyControl uKey => this[Key.U];
        public KeyControl vKey => this[Key.V];
        public KeyControl wKey => this[Key.W];
        public KeyControl xKey => this[Key.X];
        public KeyControl yKey => this[Key.Y];
        public KeyControl zKey => this[Key.Z];
        public KeyControl digit1Key => this[Key.Digit1];
        public KeyControl digit2Key => this[Key.Digit2];
        public KeyControl digit3Key => this[Key.Digit3];
        public KeyControl digit4Key => this[Key.Digit4];
        public KeyControl digit5Key => this[Key.Digit5];
        public KeyControl digit6Key => this[Key.Digit6];
        public KeyControl digit7Key => this[Key.Digit7];
        public KeyControl digit8Key => this[Key.Digit8];
        public KeyControl digit9Key => this[Key.Digit9];
        public KeyControl digit0Key => this[Key.Digit0];
        public KeyControl leftShiftKey => this[Key.LeftShift];
        public KeyControl rightShiftKey => this[Key.RightShift];
        public KeyControl leftAltKey => this[Key.LeftAlt];
        public KeyControl rightAltKey => this[Key.RightAlt];
        public KeyControl leftCtrlKey => this[Key.LeftCtrl];
        public KeyControl rightCtrlKey => this[Key.RightCtrl];
        public KeyControl leftMetaKey => this[Key.LeftMeta];
        public KeyControl rightMetaKey => this[Key.RightMeta];
        public KeyControl escapeKey => this[Key.Escape];
        public KeyControl leftArrowKey => this[Key.LeftArrow];
        public KeyControl rightArrowKey => this[Key.RightArrow];
        public KeyControl upArrowKey => this[Key.UpArrow];
        public KeyControl downArrowKey => this[Key.DownArrow];
        public KeyControl backspaceKey => this[Key.Backspace];
        public KeyControl pageDownKey => this[Key.PageDown];
        public KeyControl pageUpKey => this[Key.PageUp];
        public KeyControl homeKey => this[Key.Home];
        public KeyControl endKey => this[Key.End];
        public KeyControl insertKey => this[Key.Insert];
        public KeyControl deleteKey => this[Key.Delete];
        public KeyControl capsLockKey => this[Key.CapsLock];
        public KeyControl f1Key => this[Key.F1];
        public KeyControl f2Key => this[Key.F2];
        public KeyControl f3Key => this[Key.F3];
        public KeyControl f4Key => this[Key.F4];
        public KeyControl f5Key => this[Key.F5];
        public KeyControl f6Key => this[Key.F6];
        public KeyControl f7Key => this[Key.F7];
        public KeyControl f8Key => this[Key.F8];
        public KeyControl f9Key => this[Key.F9];
        public KeyControl f10Key => this[Key.F10];
        public KeyControl f11Key => this[Key.F11];
        public KeyControl f12Key => this[Key.F12];
        public ButtonControl shiftKey => new ButtonControl { name = "shift", m_Query = q => leftShiftKey.m_Query(q) || rightShiftKey.m_Query(q) };
        public ButtonControl ctrlKey => new ButtonControl { name = "ctrl", m_Query = q => leftCtrlKey.m_Query(q) || rightCtrlKey.m_Query(q) };
        public ButtonControl altKey => new ButtonControl { name = "alt", m_Query = q => leftAltKey.m_Query(q) || rightAltKey.m_Query(q) };
    }

    public class Pointer : InputDevice
    {
        public Vector2Control position { get; } = new Vector2Control { name = "position", m_Read = () => { Input.MouseState(out Vector2 p, out _, out _); return p; } };
        public Vector2Control delta { get; } = new Vector2Control { name = "delta", m_Read = () => { Input.MouseState(out _, out Vector2 d, out _); return d; } };
        public ButtonControl press { get; } = new ButtonControl { name = "press", m_Query = q => Input.MouseQuery(0, q) };
    }

    public sealed class Mouse : Pointer
    {
        private static Mouse s_Current;
        public static Mouse current => s_Current ??= new Mouse { name = "Mouse" };

        // Input System reports the wheel in "clicks" of 120 on Windows.
        public Vector2Control scroll { get; } = new Vector2Control { name = "scroll", m_Read = () => { Input.MouseState(out _, out _, out float s); return new Vector2(0f, s * 120f); } };
        public ButtonControl leftButton { get; } = new ButtonControl { name = "leftButton", m_Query = q => Input.MouseQuery(0, q) };
        public ButtonControl rightButton { get; } = new ButtonControl { name = "rightButton", m_Query = q => Input.MouseQuery(1, q) };
        public ButtonControl middleButton { get; } = new ButtonControl { name = "middleButton", m_Query = q => Input.MouseQuery(2, q) };
        public ButtonControl forwardButton { get; } = new ButtonControl { name = "forwardButton", m_Query = q => Input.MouseQuery(4, q) };
        public ButtonControl backButton { get; } = new ButtonControl { name = "backButton", m_Query = q => Input.MouseQuery(3, q) };

        public void WarpCursorPosition(Vector2 position) { }
    }
}
