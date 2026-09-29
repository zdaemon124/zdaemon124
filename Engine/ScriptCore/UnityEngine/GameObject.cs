using System;
using System.Collections.Generic;
using IndeetsEngine.Interop;
using IndeetsEngine.Runtime;

namespace UnityEngine
{
    public enum PrimitiveType { Sphere = 0, Capsule = 1, Cylinder = 2, Cube = 3, Plane = 4, Quad = 5 }

    public enum SendMessageOptions { RequireReceiver = 0, DontRequireReceiver = 1 }

    /// <summary>A scene entity. Wraps an engine entity id; one wrapper per entity.</summary>
    public sealed unsafe class GameObject : Object
    {
        internal readonly uint m_Id;
        private Transform m_Transform;
        internal bool m_IsAsset; // part of a prefab asset (Resources.Load / serialized reference), not the scene

        /// <summary>Wraps an existing entity (used by the runtime).</summary>
        internal GameObject(uint id)
        {
            m_Id = id;
        }

        public GameObject() : this("New Game Object") { }

        public GameObject(string name) : this(World.CreateEntity(name))
        {
            World.Register(this);
        }

        public GameObject(string name, params Type[] components) : this(name)
        {
            foreach (Type type in components)
                AddComponent(type);
        }

        internal uint EntityId => m_Id;

        internal override bool IsAlive => !m_Destroyed && World.EntityExists(m_Id);

        public override unsafe string name
        {
            get => World.EntityExists(m_Id) ? Native.FromUtf8(Native.Api.EntityGetName(m_Id)) : base.name;
            set
            {
                fixed (byte* p = Native.Utf8(value))
                    Native.Api.EntitySetName(m_Id, p);
            }
        }

        public GameObject gameObject => this;

        public Transform transform => m_Transform ??= new Transform(this);

        public bool activeSelf => Native.Api.EntityGetActive(m_Id) != 0;

        public bool activeInHierarchy => World.IsActiveInHierarchy(m_Id);

        public void SetActive(bool value)
        {
            if (!IsAlive || activeSelf == value)
                return;
            Native.Api.EntitySetActive(m_Id, value ? 1 : 0);
            World.SyncActivation();
        }

        public string tag
        {
            get => IsAlive ? Native.FromUtf8(Native.Api.EntityGetTag(m_Id)) : "Untagged";
            set
            {
                fixed (byte* p = Native.Utf8(string.IsNullOrEmpty(value) ? "Untagged" : value))
                    Native.Api.EntitySetTag(m_Id, p);
            }
        }

        public int layer
        {
            get => IsAlive ? Native.Api.EntityGetLayer(m_Id) : 0;
            set => Native.Api.EntitySetLayer(m_Id, Mathf.Clamp(value, 0, 31));
        }

        public bool isStatic { get; set; }

        public bool CompareTag(string tag) => this.tag == tag;

        // ---- Components

        public T GetComponent<T>() => (T)(object)World.GetComponent(this, typeof(T));
        public Component GetComponent(Type type) => World.GetComponent(this, type);

        public Component GetComponent(string type)
        {
            foreach (Component c in World.ComponentsOf(this))
                if (c.GetType().Name == type)
                    return c;
            return null;
        }

        public bool TryGetComponent<T>(out T component)
        {
            component = GetComponent<T>();
            return component is Object o ? o : component != null;
        }

        public bool TryGetComponent(Type type, out Component component)
        {
            component = GetComponent(type);
            return component;
        }

        public T[] GetComponents<T>()
        {
            var list = new List<T>();
            GetComponents(list);
            return list.ToArray();
        }

        public Component[] GetComponents(Type type)
        {
            var list = new List<Component>();
            World.CollectComponents(this, type, list, false, false);
            return list.ToArray();
        }

        public void GetComponents<T>(List<T> results)
        {
            results.Clear();
            var list = new List<Component>();
            World.CollectComponents(this, typeof(T), list, false, false);
            foreach (Component c in list)
                results.Add((T)(object)c);
        }

        public T GetComponentInChildren<T>() => GetComponentInChildren<T>(false);

        public T GetComponentInChildren<T>(bool includeInactive) =>
            (T)(object)World.FindInHierarchy(this, typeof(T), includeInactive, children: true);

        public Component GetComponentInChildren(Type type, bool includeInactive = false) =>
            World.FindInHierarchy(this, type, includeInactive, children: true);

        public T GetComponentInParent<T>() => GetComponentInParent<T>(false);

        public T GetComponentInParent<T>(bool includeInactive) =>
            (T)(object)World.FindInHierarchy(this, typeof(T), includeInactive, children: false);

        public Component GetComponentInParent(Type type, bool includeInactive = false) =>
            World.FindInHierarchy(this, type, includeInactive, children: false);

        public T[] GetComponentsInChildren<T>() => GetComponentsInChildren<T>(false);

        public T[] GetComponentsInChildren<T>(bool includeInactive)
        {
            var list = new List<T>();
            GetComponentsInChildren(includeInactive, list);
            return list.ToArray();
        }

        public void GetComponentsInChildren<T>(List<T> results) => GetComponentsInChildren(false, results);

        public void GetComponentsInChildren<T>(bool includeInactive, List<T> results)
        {
            results.Clear();
            var list = new List<Component>();
            World.CollectComponents(this, typeof(T), list, true, includeInactive);
            foreach (Component c in list)
                results.Add((T)(object)c);
        }

        public Component[] GetComponentsInChildren(Type type, bool includeInactive = false)
        {
            var list = new List<Component>();
            World.CollectComponents(this, type, list, true, includeInactive);
            return list.ToArray();
        }

        public T[] GetComponentsInParent<T>(bool includeInactive = false)
        {
            var result = new List<T>();
            for (Transform t = transform; t != null; t = t.parent)
            {
                if (!includeInactive && !t.gameObject.activeInHierarchy)
                    continue;
                var list = new List<Component>();
                World.CollectComponents(t.gameObject, typeof(T), list, false, true);
                foreach (Component c in list)
                    result.Add((T)(object)c);
            }
            return result.ToArray();
        }

        public T AddComponent<T>() where T : Component => (T)World.AddComponent(this, typeof(T));
        public Component AddComponent(Type componentType) => World.AddComponent(this, componentType);

        // ---- Messages

        public void SendMessage(string methodName, object value = null, SendMessageOptions options = SendMessageOptions.RequireReceiver) =>
            World.SendMessage(this, methodName, value, options, false, false);

        public void SendMessage(string methodName, SendMessageOptions options) => SendMessage(methodName, null, options);

        public void SendMessageUpwards(string methodName, object value = null, SendMessageOptions options = SendMessageOptions.RequireReceiver) =>
            World.SendMessage(this, methodName, value, options, false, true);

        public void BroadcastMessage(string methodName, object parameter = null, SendMessageOptions options = SendMessageOptions.RequireReceiver) =>
            World.SendMessage(this, methodName, parameter, options, true, false);

        // ---- Statics

        public static unsafe GameObject Find(string name)
        {
            if (string.IsNullOrEmpty(name))
                return null;
            uint id;
            fixed (byte* p = Native.Utf8(name))
                id = Native.Api.EntityFind(p);
            GameObject go = id != 0 ? World.GetGameObject(id) : null;
            return go != null && go.activeInHierarchy ? go : null;
        }

        public static GameObject FindWithTag(string tag) => FindGameObjectWithTag(tag);

        public static GameObject FindGameObjectWithTag(string tag)
        {
            foreach (GameObject go in World.AllGameObjects())
                if (go.activeInHierarchy && go.tag == tag)
                    return go;
            return null;
        }

        public static GameObject[] FindGameObjectsWithTag(string tag)
        {
            var result = new List<GameObject>();
            foreach (GameObject go in World.AllGameObjects())
                if (go.activeInHierarchy && go.tag == tag)
                    result.Add(go);
            return result.ToArray();
        }

        public static unsafe GameObject CreatePrimitive(PrimitiveType type)
        {
            uint id;
            fixed (byte* p = Native.Utf8(type.ToString()))
                id = Native.Api.EntityCreatePrimitive((int)type, p);
            return World.GetGameObject(id);
        }
    }
}
