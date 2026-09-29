using System;
using System.Collections.Generic;
using IndeetsEngine.Runtime;

namespace UnityEngine
{
    public enum HideFlags
    {
        None = 0,
        HideInHierarchy = 1,
        HideInInspector = 2,
        DontSaveInEditor = 4,
        NotEditable = 8,
        DontSaveInBuild = 16,
        DontUnloadUnusedAsset = 32,
        DontSave = 52,
        HideAndDontSave = 61,
    }

    public enum FindObjectsInactive { Exclude = 0, Include = 1 }
    public enum FindObjectsSortMode { None = 0, InstanceID = 1 }

    /// <summary>
    /// Base class of everything the engine owns. Like Unity, a destroyed object compares equal
    /// to null ("fake null") even though the C# reference is still alive.
    /// </summary>
    public class Object
    {
        private static int s_NextInstanceId = 1;
        private readonly int m_InstanceId = s_NextInstanceId++;
        internal bool m_Destroyed;
        private string m_Name;

        public HideFlags hideFlags { get; set; }

        public virtual string name
        {
            get => m_Name ?? string.Empty;
            set => m_Name = value;
        }

        /// <summary>False once destroyed (or when the underlying engine object no longer exists).</summary>
        internal virtual bool IsAlive => !m_Destroyed;

        public int GetInstanceID() => m_InstanceId;

        public override int GetHashCode() => m_InstanceId;
        // Reference identity (plus fake null), so destroyed objects stay usable as dictionary keys.
        public override bool Equals(object other) => other is null ? !IsAlive : ReferenceEquals(this, other);
        public override string ToString() => $"{name} ({GetType().Name})";

        public static bool operator ==(Object x, Object y)
        {
            bool xNull = x is null || !x.IsAlive;
            bool yNull = y is null || !y.IsAlive;
            if (xNull || yNull)
                return xNull && yNull;
            return ReferenceEquals(x, y);
        }

        public static bool operator !=(Object x, Object y) => !(x == y);

        public static implicit operator bool(Object exists) => !(exists is null) && exists.IsAlive;

        // ---- Destruction

        public static void Destroy(Object obj) => Destroy(obj, 0f);

        public static void Destroy(Object obj, float t)
        {
            if (obj is null)
                return;
            World.ScheduleDestroy(obj, t);
        }

        public static void DestroyImmediate(Object obj) => DestroyImmediate(obj, false);

        public static void DestroyImmediate(Object obj, bool allowDestroyingAssets)
        {
            if (obj is null)
                return;
            World.DestroyNow(obj);
        }

        public static void DontDestroyOnLoad(Object target)
        {
            // There is a single scene at a time for now; nothing is unloaded under a script.
        }

        /// <summary>Engine hook: called once when the object is really destroyed.</summary>
        internal virtual void OnEngineDestroy() => m_Destroyed = true;

        // ---- Instantiation

        public static T Instantiate<T>(T original) where T : Object => (T)World.Instantiate(original, null, false, null, null);

        public static T Instantiate<T>(T original, Transform parent) where T : Object =>
            (T)World.Instantiate(original, parent, false, null, null);

        public static T Instantiate<T>(T original, Transform parent, bool worldPositionStays) where T : Object =>
            (T)World.Instantiate(original, parent, worldPositionStays, null, null);

        public static T Instantiate<T>(T original, Vector3 position, Quaternion rotation) where T : Object =>
            (T)World.Instantiate(original, null, true, position, rotation);

        public static T Instantiate<T>(T original, Vector3 position, Quaternion rotation, Transform parent) where T : Object =>
            (T)World.Instantiate(original, parent, true, position, rotation);

        public static Object Instantiate(Object original) => World.Instantiate(original, null, false, null, null);
        public static Object Instantiate(Object original, Transform parent) => World.Instantiate(original, parent, false, null, null);
        public static Object Instantiate(Object original, Vector3 position, Quaternion rotation) =>
            World.Instantiate(original, null, true, position, rotation);

        // ---- Queries

        public static T FindFirstObjectByType<T>() where T : Object => FindFirstObjectByType<T>(FindObjectsInactive.Exclude);

        public static T FindFirstObjectByType<T>(FindObjectsInactive findObjectsInactive) where T : Object
        {
            foreach (Object o in World.FindObjects(typeof(T), findObjectsInactive == FindObjectsInactive.Include))
                return (T)o;
            return null;
        }

        public static T FindAnyObjectByType<T>() where T : Object => FindFirstObjectByType<T>(FindObjectsInactive.Exclude);

        public static T FindAnyObjectByType<T>(FindObjectsInactive findObjectsInactive) where T : Object =>
            FindFirstObjectByType<T>(findObjectsInactive);

        public static T FindObjectOfType<T>() where T : Object => FindFirstObjectByType<T>();
        public static T FindObjectOfType<T>(bool includeInactive) where T : Object =>
            FindFirstObjectByType<T>(includeInactive ? FindObjectsInactive.Include : FindObjectsInactive.Exclude);

        public static T[] FindObjectsByType<T>(FindObjectsSortMode sortMode) where T : Object =>
            FindObjectsByType<T>(FindObjectsInactive.Exclude, sortMode);

        public static T[] FindObjectsByType<T>(FindObjectsInactive findObjectsInactive, FindObjectsSortMode sortMode = FindObjectsSortMode.None)
            where T : Object
        {
            var result = new List<T>();
            foreach (Object o in World.FindObjects(typeof(T), findObjectsInactive == FindObjectsInactive.Include))
                result.Add((T)o);
            return result.ToArray();
        }

        public static T[] FindObjectsOfType<T>() where T : Object => FindObjectsByType<T>(FindObjectsInactive.Exclude);
        public static T[] FindObjectsOfType<T>(bool includeInactive) where T : Object =>
            FindObjectsByType<T>(includeInactive ? FindObjectsInactive.Include : FindObjectsInactive.Exclude);

        public static Object FindFirstObjectByType(Type type)
        {
            foreach (Object o in World.FindObjects(type, false))
                return o;
            return null;
        }

        public static Object[] FindObjectsByType(Type type, FindObjectsSortMode sortMode) =>
            new List<Object>(World.FindObjects(type, false)).ToArray();
    }

    /// <summary>ScriptableObject: plain data objects. Asset loading comes with the Unity asset importer.</summary>
    public class ScriptableObject : Object
    {
        public static T CreateInstance<T>() where T : ScriptableObject => (T)CreateInstance(typeof(T));

        public static ScriptableObject CreateInstance(Type type)
        {
            var instance = (ScriptableObject)Activator.CreateInstance(type, true);
            World.InvokeMessage(instance, "Awake");
            World.InvokeMessage(instance, "OnEnable");
            return instance;
        }
    }
}
