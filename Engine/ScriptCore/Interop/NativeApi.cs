using System;
using System.Runtime.InteropServices;
using System.Text;

namespace IndeetsEngine.Interop
{
    /// <summary>
    /// Built-in (native) component kinds. Must match ie::ScriptBuiltin in ScriptEngine.cpp.
    /// </summary>
    internal enum BuiltinKind
    {
        MeshRenderer = 1,
        Collider = 2,
        Rigidbody = 3,
        Light = 4,
        Camera = 5,
    }

    /// <summary>A physics contact; layout of ie::ContactEvent (PhysicsWorld.h).</summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct ContactEvent
    {
        public uint A;
        public uint B;
        public int Type;     // 0 enter, 1 stay, 2 exit
        public int Trigger;
        public float PointX, PointY, PointZ;
        public float NormalX, NormalY, NormalZ;          // from A to B
        public float VelocityX, VelocityY, VelocityZ;    // B relative to A
    }

    /// <summary>
    /// Function table the native engine hands to <see cref="Bridge.Initialize"/>.
    /// The field order is the ABI: it must match ie::ScriptNativeApi exactly.
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct NativeApi
    {
        public delegate* unmanaged<int, byte*, void> Log;
        public delegate* unmanaged<byte*, void> SetResult;

        public delegate* unmanaged<uint, int> EntityExists;
        public delegate* unmanaged<byte*, uint> EntityCreate;
        public delegate* unmanaged<int, byte*, uint> EntityCreatePrimitive;
        public delegate* unmanaged<uint, void> EntityDestroy;
        public delegate* unmanaged<uint, uint, int, uint> EntityInstantiate;
        public delegate* unmanaged<uint, uint*, int, int> EntityGetSubtree;
        public delegate* unmanaged<byte*, uint> EntityFind;
        public delegate* unmanaged<uint, byte*> EntityGetName;
        public delegate* unmanaged<uint, byte*, void> EntitySetName;
        public delegate* unmanaged<uint, int> EntityGetActive;
        public delegate* unmanaged<uint, int, void> EntitySetActive;
        public delegate* unmanaged<uint*, int, byte*, void> EntityGetActiveStates;
        public delegate* unmanaged<uint, uint> EntityGetParent;
        public delegate* unmanaged<uint, uint, int, void> EntitySetParent;
        public delegate* unmanaged<uint, uint*, int, int> EntityGetChildren;
        public delegate* unmanaged<uint*, int, int> EntityGetAll;

        public delegate* unmanaged<uint, float*, float*, float*, void> TransformGetLocal;
        public delegate* unmanaged<uint, float*, float*, float*, void> TransformSetLocal;
        public delegate* unmanaged<uint, float*, float*, float*, void> TransformGetWorld;
        public delegate* unmanaged<uint, float*, float*, void> TransformSetWorld;

        public delegate* unmanaged<uint, int, int> ComponentHas;
        public delegate* unmanaged<uint, int, void> ComponentAdd;
        public delegate* unmanaged<uint, int, void> ComponentRemove;
        public delegate* unmanaged<uint, int, int, float*, int> ComponentGet;
        public delegate* unmanaged<uint, int, int, float*, void> ComponentSet;
        public delegate* unmanaged<uint, int, int, byte*> ComponentGetString;
        public delegate* unmanaged<uint, int, int, byte*, void> ComponentSetString;

        public delegate* unmanaged<uint, float*, int, void> RigidbodyAddForce;
        public delegate* unmanaged<float*, float*, float, float*, uint*, int> PhysicsRaycast;

        public delegate* unmanaged<int, int, int> InputKey;
        public delegate* unmanaged<int, int, int> InputMouseButton;
        public delegate* unmanaged<float*, void> InputMouseState;
        public delegate* unmanaged<int, void> InputSetCursorLocked;
        public delegate* unmanaged<float*, void> ScreenSize;
        public delegate* unmanaged<uint> MainCamera;

        public delegate* unmanaged<uint, byte*> EntityGetTag;
        public delegate* unmanaged<uint, byte*, void> EntitySetTag;
        public delegate* unmanaged<uint, int> EntityGetLayer;
        public delegate* unmanaged<uint, int, void> EntitySetLayer;
        public delegate* unmanaged<uint, byte*> EntityGetScripts;
        public delegate* unmanaged<byte*, int, byte*> AssetFindResources;
        public delegate* unmanaged<byte*, byte*> AssetDescribe;
        public delegate* unmanaged<byte*, uint> AssetPrefabTemplate;
    }

    /// <summary>Access to the native function table plus UTF-8 helpers.</summary>
    internal static unsafe class Native
    {
        private static NativeApi* api;

        public static bool IsAvailable => api != null;

        public static ref NativeApi Api => ref *api;

        public static void Attach(NativeApi* table) => api = table;

        /// <summary>Null-terminated UTF-8 copy of a string, for passing to native code.</summary>
        public static byte[] Utf8(string text)
        {
            text ??= string.Empty;
            byte[] bytes = new byte[Encoding.UTF8.GetByteCount(text) + 1];
            Encoding.UTF8.GetBytes(text, 0, text.Length, bytes, 0);
            return bytes;
        }

        public static string FromUtf8(byte* text) =>
            text == null ? string.Empty : Marshal.PtrToStringUTF8((IntPtr)text) ?? string.Empty;

        public static void Log(int level, string message)
        {
            if (api == null)
            {
                Console.WriteLine(message);
                return;
            }

            fixed (byte* p = Utf8(message))
                api->Log(level, p);
        }

        /// <summary>Returns a string to the native caller of the current bridge call.</summary>
        public static void SetResult(string text)
        {
            if (api == null)
                return;
            fixed (byte* p = Utf8(text))
                api->SetResult(p);
        }
    }
}
