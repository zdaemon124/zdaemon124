using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Text.Json.Nodes;
using IndeetsEngine.Interop;
using UnityEngine;
using Object = UnityEngine.Object;

namespace IndeetsEngine.Runtime
{
    /// <summary>Per-script lifecycle bookkeeping.</summary>
    internal sealed class ScriptState
    {
        public int Order;
        public long Sequence;
        public bool Awoken;
        public bool Started;
        public bool EnabledActive; // OnEnable has run and OnDisable has not
        public ScriptMethods Methods;
        public Action Update;
        public Action LateUpdate;
        public Action FixedUpdate;
    }

    /// <summary>Lifecycle methods a script type declares (any access level, inherited too).</summary>
    internal sealed class ScriptMethods
    {
        public MethodInfo Awake, OnEnable, Start, Update, LateUpdate, FixedUpdate, OnDisable, OnDestroy,
            OnApplicationQuit, OnApplicationPause, OnApplicationFocus;
        // Physics messages, indexed [trigger ? 1 : 0, enter/stay/exit].
        public readonly MethodInfo[,] Contact = new MethodInfo[2, 3];
        public bool HasContactMessages;
        public int ExecutionOrder;
    }

    /// <summary>
    /// The managed view of the running scene: GameObject wrappers, components, script lifecycle,
    /// destruction and instantiation. Everything runs on the engine's main thread.
    /// </summary>
    internal static unsafe class World
    {
        private static readonly Dictionary<uint, GameObject> s_Objects = new Dictionary<uint, GameObject>();
        private static readonly Dictionary<uint, List<Component>> s_Components = new Dictionary<uint, List<Component>>();
        private static readonly List<MonoBehaviour> s_Behaviours = new List<MonoBehaviour>();
        // Lifecycle passes iterate a copy of the script list (scripts add and destroy scripts while
        // they run). Copies come from a pool because passes nest: SetActive inside Update re-syncs.
        private static readonly Stack<List<MonoBehaviour>> s_SnapshotPool = new Stack<List<MonoBehaviour>>();
        private static readonly List<(Object obj, float time)> s_DestroyQueue = new List<(Object, float)>();
        private static readonly Dictionary<Type, ScriptMethods> s_MethodCache = new Dictionary<Type, ScriptMethods>();
        private static readonly Dictionary<uint, bool> s_ActiveCache = new Dictionary<uint, bool>();
        private static long s_Sequence;
        private static bool s_Syncing;
        private static bool s_SyncAgain;
        private static bool s_QuitRequested;

        public static bool IsPlaying { get; private set; }

        // ================================================================ entities

        public static bool EntityExists(uint id) => id != 0 && Native.IsAvailable && Native.Api.EntityExists(id) != 0;

        public static unsafe uint CreateEntity(string name)
        {
            fixed (byte* p = Native.Utf8(name))
                return Native.Api.EntityCreate(p);
        }

        public static void Register(GameObject go) => s_Objects[go.m_Id] = go;

        public static GameObject GetGameObject(uint id)
        {
            if (id == 0)
                return null;
            if (s_Objects.TryGetValue(id, out GameObject go) && !go.m_Destroyed)
                return go;
            if (!EntityExists(id))
                return null;
            go = new GameObject(id);
            s_Objects[id] = go;
            return go;
        }

        private static uint[] AllEntityIds()
        {
            int count = Native.Api.EntityGetAll(null, 0);
            uint[] ids = new uint[count];
            fixed (uint* p = ids)
                count = Native.Api.EntityGetAll(p, ids.Length);
            return count == ids.Length ? ids : ids.Take(count).ToArray();
        }

        public static IEnumerable<GameObject> AllGameObjects()
        {
            foreach (uint id in AllEntityIds())
            {
                GameObject go = GetGameObject(id);
                if (go != null)
                    yield return go;
            }
        }

        public static unsafe uint[] Subtree(uint root)
        {
            int count = Native.Api.EntityGetSubtree(root, null, 0);
            uint[] ids = new uint[count];
            fixed (uint* p = ids)
                count = Native.Api.EntityGetSubtree(root, p, ids.Length);
            return count == ids.Length ? ids : ids.Take(count).ToArray();
        }

        public static bool IsActiveInHierarchy(uint id)
        {
            if (!EntityExists(id))
                return false;
            unsafe
            {
                byte state = 0;
                Native.Api.EntityGetActiveStates(&id, 1, &state);
                return state != 0;
            }
        }

        // ================================================================ components

        public static List<Component> ComponentsOf(GameObject go)
        {
            if (!s_Components.TryGetValue(go.m_Id, out List<Component> list))
            {
                list = new List<Component>();
                s_Components[go.m_Id] = list;
            }
            return list;
        }

        private static BuiltinKind? KindOf(Type type)
        {
            if (typeof(Rigidbody).IsAssignableFrom(type)) return BuiltinKind.Rigidbody;
            if (typeof(Collider).IsAssignableFrom(type)) return BuiltinKind.Collider;
            if (typeof(Renderer).IsAssignableFrom(type) || type == typeof(MeshFilter)) return BuiltinKind.MeshRenderer;
            if (typeof(Light).IsAssignableFrom(type)) return BuiltinKind.Light;
            if (typeof(Camera).IsAssignableFrom(type)) return BuiltinKind.Camera;
            return null;
        }

        private static Type ConcreteBuiltinType(GameObject go, BuiltinKind kind, Type requested)
        {
            switch (kind)
            {
                case BuiltinKind.Collider:
                {
                    float shape;
                    unsafe
                    {
                        float* v = stackalloc float[4];
                        Native.Api.ComponentGet(go.m_Id, (int)kind, Collider.PropShape, v);
                        shape = v[0];
                    }
                    Type concrete = (int)shape switch { 0 => typeof(BoxCollider), 1 => typeof(SphereCollider), _ => typeof(CapsuleCollider) };
                    return requested.IsAssignableFrom(concrete) ? concrete : null;
                }
                case BuiltinKind.MeshRenderer:
                    if (requested == typeof(MeshFilter)) return typeof(MeshFilter);
                    return requested.IsAssignableFrom(typeof(MeshRenderer)) ? typeof(MeshRenderer) : null;
                case BuiltinKind.Rigidbody: return typeof(Rigidbody);
                case BuiltinKind.Light: return typeof(Light);
                case BuiltinKind.Camera: return typeof(Camera);
            }
            return null;
        }

        /// <summary>The wrapper for a native component, one instance per GameObject and type.</summary>
        private static NativeComponent GetBuiltin(GameObject go, Type requested)
        {
            BuiltinKind? kind = KindOf(requested);
            if (kind == null || Native.Api.ComponentHas(go.m_Id, (int)kind.Value) == 0)
                return null;
            Type concrete = ConcreteBuiltinType(go, kind.Value, requested);
            if (concrete == null)
                return null;
            List<Component> list = ComponentsOf(go);
            foreach (Component c in list)
                if (c.GetType() == concrete && !c.m_Destroyed)
                    return (NativeComponent)c;
            var wrapper = (NativeComponent)Activator.CreateInstance(concrete, true);
            wrapper.m_GameObject = go;
            list.Add(wrapper);
            return wrapper;
        }

        private static readonly BuiltinKind[] s_BuiltinOrder =
            { BuiltinKind.MeshRenderer, BuiltinKind.Collider, BuiltinKind.Rigidbody, BuiltinKind.Light, BuiltinKind.Camera };

        public static Component GetComponent(GameObject go, Type type)
        {
            if (go is null || !go.IsAlive || type == null)
                return null;
            if (type.IsAssignableFrom(typeof(Transform)))
                return go.transform;
            if (KindOf(type) != null)
                return GetBuiltin(go, type);
            if (s_Components.TryGetValue(go.m_Id, out List<Component> list))
                foreach (Component c in list)
                    if (!c.m_Destroyed && !(c is NativeComponent) && type.IsInstanceOfType(c))
                        return c;
            return null;
        }

        public static void CollectComponents(GameObject go, Type type, List<Component> results, bool recursive, bool includeInactive)
        {
            if (go is null || !go.IsAlive)
                return;
            if (recursive)
            {
                foreach (uint id in Subtree(go.m_Id))
                {
                    GameObject child = GetGameObject(id);
                    if (child == null || (!includeInactive && !child.activeInHierarchy))
                        continue;
                    CollectComponents(child, type, results, false, true);
                }
                return;
            }

            if (type.IsAssignableFrom(typeof(Transform)))
                results.Add(go.transform);
            foreach (BuiltinKind kind in s_BuiltinOrder)
            {
                if (Native.Api.ComponentHas(go.m_Id, (int)kind) == 0)
                    continue;
                if (type == typeof(MeshFilter))
                {
                    if (kind == BuiltinKind.MeshRenderer)
                        results.Add(GetBuiltin(go, typeof(MeshFilter)));
                    continue;
                }
                Type concrete = ConcreteBuiltinType(go, kind, typeof(Component));
                if (concrete != null && type.IsAssignableFrom(concrete))
                    results.Add(GetBuiltin(go, concrete));
            }
            if (s_Components.TryGetValue(go.m_Id, out List<Component> list))
                foreach (Component c in list)
                    if (!c.m_Destroyed && !(c is NativeComponent) && type.IsInstanceOfType(c))
                        results.Add(c);
        }

        public static Component FindInHierarchy(GameObject go, Type type, bool includeInactive, bool children)
        {
            if (go is null || !go.IsAlive)
                return null;
            if (children)
            {
                foreach (uint id in Subtree(go.m_Id))
                {
                    GameObject g = GetGameObject(id);
                    if (g == null || (!includeInactive && !g.activeInHierarchy))
                        continue;
                    Component c = GetComponent(g, type);
                    if (c)
                        return c;
                }
                return null;
            }
            for (Transform t = go.transform; t != null; t = t.parent)
            {
                if (!includeInactive && !t.gameObject.activeInHierarchy)
                    continue;
                Component c = GetComponent(t.gameObject, type);
                if (c)
                    return c;
            }
            return null;
        }

        public static IEnumerable<Object> FindObjects(Type type, bool includeInactive)
        {
            if (type == typeof(GameObject))
            {
                foreach (GameObject go in AllGameObjects())
                    if (includeInactive || go.activeInHierarchy)
                        yield return go;
                yield break;
            }
            if (typeof(MonoBehaviour).IsAssignableFrom(type) || type.IsInterface)
            {
                foreach (MonoBehaviour b in s_Behaviours.ToArray())
                    if (b.IsAlive && type.IsInstanceOfType(b) && (includeInactive || b.m_GameObject.activeInHierarchy))
                        yield return b;
                yield break;
            }
            if (typeof(Component).IsAssignableFrom(type))
            {
                var found = new List<Component>();
                foreach (GameObject go in AllGameObjects())
                {
                    if (!includeInactive && !go.activeInHierarchy)
                        continue;
                    found.Clear();
                    CollectComponents(go, type, found, false, true);
                    foreach (Component c in found)
                        yield return c;
                }
            }
        }

        public static unsafe Component AddComponent(GameObject go, Type type)
        {
            if (go is null || !go.IsAlive)
                throw new MissingReferenceException("The object of type 'GameObject' has been destroyed.");
            if (type == null || !typeof(Component).IsAssignableFrom(type))
                throw new ArgumentException("AddComponent requires a Component type.");
            if (type == typeof(Transform))
                return go.transform;

            BuiltinKind? kind = KindOf(type);
            if (kind != null)
            {
                if (type.IsAbstract || type == typeof(Collider) || type == typeof(Renderer))
                    type = kind == BuiltinKind.Collider ? typeof(BoxCollider) : typeof(MeshRenderer);
                bool had = Native.Api.ComponentHas(go.m_Id, (int)kind.Value) != 0;
                if (!had)
                    Native.Api.ComponentAdd(go.m_Id, (int)kind.Value);
                if (kind == BuiltinKind.Collider && !had)
                {
                    float* v = stackalloc float[4];
                    v[0] = type == typeof(SphereCollider) ? 1f : type == typeof(CapsuleCollider) ? 2f : 0f;
                    Native.Api.ComponentSet(go.m_Id, (int)kind.Value, Collider.PropShape, v);
                }
                return GetBuiltin(go, type);
            }

            if (type.IsAbstract || type.ContainsGenericParameters)
            {
                Debug.LogError($"Cannot add component of abstract or generic type '{type.Name}'.");
                return null;
            }
            if (!typeof(MonoBehaviour).IsAssignableFrom(type))
            {
                Debug.LogError($"Component type '{type.Name}' is not supported by the engine yet.");
                return null;
            }
            if (type.GetCustomAttribute<DisallowMultipleComponent>(true) != null && GetComponent(go, type) != null)
            {
                Debug.LogWarning($"Can't add '{type.Name}' to {go.name} because a '{type.Name}' is already added and multiple are not allowed.");
                return null;
            }
            foreach (RequireComponent req in type.GetCustomAttributes<RequireComponent>(true))
                foreach (Type required in new[] { req.m_Type0, req.m_Type1, req.m_Type2 })
                    if (required != null && GetComponent(go, required) == null)
                        AddComponent(go, required);

            MonoBehaviour behaviour = CreateBehaviour(go, type, null, true);
            if (behaviour != null && IsPlaying)
                Activate(behaviour, IsActiveInHierarchy(go.m_Id));
            return behaviour;
        }

        /// <summary>Creates and registers a script instance (no lifecycle calls yet).</summary>
        public static MonoBehaviour CreateBehaviour(GameObject go, Type type, JsonObject fields, bool enabled)
        {
            MonoBehaviour behaviour;
            try
            {
                behaviour = (MonoBehaviour)Activator.CreateInstance(type, true);
            }
            catch (Exception e)
            {
                Debug.LogError($"Could not create script '{type.Name}': {DescribeException(e)}");
                return null;
            }
            behaviour.m_GameObject = go;
            behaviour.m_Enabled = enabled;
            ScriptMethods methods = MethodsOf(type);
            var state = new ScriptState
            {
                Order = methods.ExecutionOrder,
                Sequence = ++s_Sequence,
                Methods = methods,
                Update = Bind(behaviour, methods.Update),
                LateUpdate = Bind(behaviour, methods.LateUpdate),
                FixedUpdate = Bind(behaviour, methods.FixedUpdate),
            };
            behaviour.m_State = state;
            if (fields != null)
                FieldCodec.ReadInto(behaviour, fields, null);
            ComponentsOf(go).Add(behaviour);
            InsertSorted(behaviour);
            return behaviour;
        }

        private static void InsertSorted(MonoBehaviour behaviour)
        {
            ScriptState s = behaviour.m_State;
            int lo = 0, hi = s_Behaviours.Count;
            while (lo < hi)
            {
                int mid = (lo + hi) / 2;
                ScriptState m = s_Behaviours[mid].m_State;
                if (m.Order < s.Order || (m.Order == s.Order && m.Sequence < s.Sequence))
                    lo = mid + 1;
                else
                    hi = mid;
            }
            s_Behaviours.Insert(lo, behaviour);
        }

        private static Action Bind(MonoBehaviour target, MethodInfo method) =>
            method == null ? null : (Action)Delegate.CreateDelegate(typeof(Action), target, method, false);

        public static ScriptMethods MethodsOf(Type type)
        {
            if (s_MethodCache.TryGetValue(type, out ScriptMethods cached))
                return cached;
            var m = new ScriptMethods
            {
                Awake = FindMethod(type, "Awake", 0),
                OnEnable = FindMethod(type, "OnEnable", 0),
                Start = FindMethod(type, "Start", 0),
                Update = FindMethod(type, "Update", 0),
                LateUpdate = FindMethod(type, "LateUpdate", 0),
                FixedUpdate = FindMethod(type, "FixedUpdate", 0),
                OnDisable = FindMethod(type, "OnDisable", 0),
                OnDestroy = FindMethod(type, "OnDestroy", 0),
                OnApplicationQuit = FindMethod(type, "OnApplicationQuit", 0),
                OnApplicationPause = FindMethod(type, "OnApplicationPause", 1),
                OnApplicationFocus = FindMethod(type, "OnApplicationFocus", 1),
                ExecutionOrder = type.GetCustomAttribute<DefaultExecutionOrder>(true)?.order ?? 0,
            };
            string[,] contactNames =
            {
                { "OnCollisionEnter", "OnCollisionStay", "OnCollisionExit" },
                { "OnTriggerEnter", "OnTriggerStay", "OnTriggerExit" },
            };
            for (int t = 0; t < 2; t++)
            {
                for (int k = 0; k < 3; k++)
                {
                    // Unity accepts the message with its argument or without any.
                    m.Contact[t, k] = FindMethod(type, contactNames[t, k], 1) ?? FindMethod(type, contactNames[t, k], 0);
                    m.HasContactMessages |= m.Contact[t, k] != null;
                }
            }
            // Update/LateUpdate/FixedUpdate must be void and parameterless to be bound as Action.
            if (m.Update != null && m.Update.ReturnType != typeof(void)) m.Update = null;
            if (m.LateUpdate != null && m.LateUpdate.ReturnType != typeof(void)) m.LateUpdate = null;
            if (m.FixedUpdate != null && m.FixedUpdate.ReturnType != typeof(void)) m.FixedUpdate = null;
            s_MethodCache[type] = m;
            return m;
        }

        public static MethodInfo FindMethod(Type type, string name, int parameterCount)
        {
            const BindingFlags Flags = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly;
            for (Type t = type; t != null && t != typeof(MonoBehaviour) && t != typeof(ScriptableObject) && t != typeof(object); t = t.BaseType)
                foreach (MethodInfo mi in t.GetMethods(Flags))
                    if (mi.Name == name && mi.GetParameters().Length == parameterCount && !mi.IsGenericMethodDefinition)
                        return mi;
            return null;
        }

        public static void ClearTypeCaches()
        {
            s_MethodCache.Clear();
            FieldCodec.ClearCaches();
        }

        // ================================================================ lifecycle

        public static void Call(Object target, MethodInfo method, object[] args = null)
        {
            if (method == null)
                return;
            try
            {
                object result = method.Invoke(target, args);
                if (result is IEnumerator routine && target is MonoBehaviour mb && mb.IsAlive)
                    CoroutineScheduler.Start(mb, routine);
            }
            catch (Exception e)
            {
                Debug.LogException(e is TargetInvocationException tie && tie.InnerException != null ? tie.InnerException : e);
            }
        }

        private static void Call(Action action)
        {
            try
            {
                action();
            }
            catch (Exception e)
            {
                Debug.LogException(e);
            }
        }

        public static void InvokeMessage(Object target, string name)
        {
            MethodInfo method = FindMethod(target.GetType(), name, 0);
            if (method != null)
                Call(target, method);
        }

        /// <summary>Awake / OnEnable / OnDisable for one script given its object's activity.</summary>
        private static void Activate(MonoBehaviour b, bool activeInHierarchy)
        {
            ScriptState s = b.m_State;
            if (s == null || b.m_Destroyed)
                return;
            if (!activeInHierarchy)
            {
                if (s.EnabledActive)
                {
                    s.EnabledActive = false;
                    Call(b, s.Methods.OnDisable);
                }
                return;
            }
            if (!s.Awoken)
            {
                s.Awoken = true;
                Call(b, s.Methods.Awake);
                if (b.m_Destroyed)
                    return;
            }
            if (b.m_Enabled && !s.EnabledActive)
            {
                s.EnabledActive = true;
                Call(b, s.Methods.OnEnable);
            }
            else if (!b.m_Enabled && s.EnabledActive)
            {
                s.EnabledActive = false;
                Call(b, s.Methods.OnDisable);
            }
        }

        public static void OnBehaviourEnabledChanged(MonoBehaviour b)
        {
            if (IsPlaying && b.m_State != null && b.IsAlive && b.m_State.Awoken)
                Activate(b, IsActiveInHierarchy(b.m_GameObject.m_Id));
        }

        /// <summary>
        /// Brings every script in line with its object's activity (SetActive, re-parenting,
        /// the editor toggling a checkbox): Awake on first activation, OnEnable / OnDisable on changes.
        /// </summary>
        public static unsafe void SyncActivation()
        {
            if (!IsPlaying)
                return;
            if (s_Syncing)
            {
                s_SyncAgain = true;
                return;
            }
            s_Syncing = true;
            try
            {
                do
                {
                    s_SyncAgain = false;
                    List<MonoBehaviour> snapshot = TakeSnapshot();
                    int n = snapshot.Count;
                    if (n == 0)
                    {
                        Release(snapshot);
                        break;
                    }
                    uint[] ids = new uint[n];
                    byte[] states = new byte[n];
                    for (int i = 0; i < n; i++)
                        ids[i] = snapshot[i].m_GameObject.m_Id;
                    fixed (uint* pi = ids)
                    fixed (byte* ps = states)
                        Native.Api.EntityGetActiveStates(pi, n, ps);
                    s_ActiveCache.Clear();
                    for (int i = 0; i < n; i++)
                        s_ActiveCache[ids[i]] = states[i] != 0;

                    for (int i = 0; i < n; i++)
                    {
                        MonoBehaviour b = snapshot[i];
                        if (b.m_Destroyed)
                            continue;
                        bool active = states[i] != 0 && EntityExists(ids[i]);
                        bool wasEnabledActive = b.m_State.EnabledActive;
                        Activate(b, active);
                        // Deactivating the object stops its coroutines, as in Unity.
                        if (!active && wasEnabledActive)
                            CoroutineScheduler.StopAll(b);
                    }
                    Release(snapshot);
                } while (s_SyncAgain);
            }
            finally
            {
                s_Syncing = false;
            }
        }

        private static List<MonoBehaviour> TakeSnapshot()
        {
            List<MonoBehaviour> list = s_SnapshotPool.Count > 0 ? s_SnapshotPool.Pop() : new List<MonoBehaviour>();
            list.AddRange(s_Behaviours);
            return list;
        }

        private static void Release(List<MonoBehaviour> list)
        {
            list.Clear();
            s_SnapshotPool.Push(list);
        }

        private static void RunStarts()
        {
            List<MonoBehaviour> snapshot = TakeSnapshot();
            foreach (MonoBehaviour b in snapshot)
            {
                ScriptState s = b.m_State;
                if (b.m_Destroyed || s.Started || !s.EnabledActive)
                    continue;
                s.Started = true;
                Call(b, s.Methods.Start);
            }
            Release(snapshot);
        }

        public static void FixedUpdate(float fixedDeltaTime)
        {
            Time.inFixedTimeStep = true;
            float savedDelta = Time.deltaTime;
            Time.fixedDeltaTime = fixedDeltaTime;
            Time.deltaTime = fixedDeltaTime * Time.timeScale;
            Time.fixedTime += Time.deltaTime;
            Time.fixedUnscaledTime += fixedDeltaTime;
            try
            {
                RunStarts();
                List<MonoBehaviour> snapshot = TakeSnapshot();
                foreach (MonoBehaviour b in snapshot)
                {
                    ScriptState s = b.m_State;
                    if (!b.m_Destroyed && s.EnabledActive && s.Started && s.FixedUpdate != null)
                        Call(s.FixedUpdate);
                }
                Release(snapshot);
                CoroutineScheduler.Tick(CoroutineScheduler.Phase.FixedUpdate);
            }
            finally
            {
                Time.deltaTime = savedDelta;
                Time.inFixedTimeStep = false;
            }
        }

        public static void Update(float unscaledDeltaTime)
        {
            unscaledDeltaTime = MathF.Max(0f, unscaledDeltaTime);
            Time.frameCount++;
            Time.unscaledDeltaTime = unscaledDeltaTime;
            Time.unscaledTime += unscaledDeltaTime;
            Time.deltaTime = MathF.Min(unscaledDeltaTime, Time.maximumDeltaTime) * Time.timeScale;
            Time.time += Time.deltaTime;
            Time.smoothDeltaTime = Time.frameCount <= 1 ? Time.deltaTime : Mathf.Lerp(Time.smoothDeltaTime, Time.deltaTime, 0.2f);

            SyncActivation();
            RunStarts();
            List<MonoBehaviour> snapshot = TakeSnapshot();
            foreach (MonoBehaviour b in snapshot)
            {
                ScriptState s = b.m_State;
                if (!b.m_Destroyed && s.EnabledActive && s.Started && s.Update != null)
                    Call(s.Update);
            }
            Release(snapshot);
            InvokeScheduler.Tick();
            CoroutineScheduler.Tick(CoroutineScheduler.Phase.Update);
        }

        public static void LateUpdate()
        {
            List<MonoBehaviour> snapshot = TakeSnapshot();
            foreach (MonoBehaviour b in snapshot)
            {
                ScriptState s = b.m_State;
                if (!b.m_Destroyed && s.EnabledActive && s.Started && s.LateUpdate != null)
                    Call(s.LateUpdate);
            }
            Release(snapshot);
            CoroutineScheduler.Tick(CoroutineScheduler.Phase.EndOfFrame);
            ProcessDestroyQueue();
        }

        // ================================================================ play mode

        public static void BeginPlay(JsonNode scene)
        {
            ResetPlayState();
            IsPlaying = true;
            s_QuitRequested = false;
            Time.time = Time.unscaledTime = Time.fixedTime = Time.fixedUnscaledTime = 0f;
            Time.deltaTime = Time.unscaledDeltaTime = 0f;
            Time.frameCount = 0;
            Time.timeScale = 1f;
            Cursor.Reset();
            UnityEngine.Input.ResetState();

            RunInitializers(RuntimeInitializeLoadType.SubsystemRegistration);
            RunInitializers(RuntimeInitializeLoadType.AfterAssembliesLoaded);
            RunInitializers(RuntimeInitializeLoadType.BeforeSplashScreen);
            RunInitializers(RuntimeInitializeLoadType.BeforeSceneLoad);

            // Pass 1: create every script so references between them resolve in pass 2.
            var pending = new List<(MonoBehaviour behaviour, JsonObject fields)>();
            if (scene?["entities"] is JsonArray entities)
            {
                foreach (JsonNode entityNode in entities)
                {
                    uint id = (uint)FieldCodec.Number(entityNode?["id"]);
                    GameObject go = GetGameObject(id);
                    if (go == null || !(entityNode["scripts"] is JsonArray scripts))
                        continue;
                    foreach (JsonNode scriptNode in scripts)
                    {
                        string className = scriptNode?["class"]?.GetValue<string>();
                        Type type = ScriptDomain.FindScriptType(className);
                        if (type == null)
                        {
                            Debug.LogWarning($"The referenced script '{className}' on '{go.name}' is missing (not compiled or renamed).");
                            continue;
                        }
                        bool enabled = scriptNode["enabled"]?.GetValue<bool>() ?? true;
                        MonoBehaviour b = CreateBehaviour(go, type, null, enabled);
                        if (b != null)
                            pending.Add((b, scriptNode["fields"] as JsonObject));
                    }
                }
            }
            foreach (var (behaviour, fields) in pending)
                if (fields != null)
                    FieldCodec.ReadInto(behaviour, fields, null);

            SyncActivation();
            RunInitializers(RuntimeInitializeLoadType.AfterSceneLoad);
        }

        public static void EndPlay()
        {
            if (!IsPlaying)
                return;
            List<MonoBehaviour> snapshot = TakeSnapshot();
            foreach (MonoBehaviour b in snapshot)
                if (!b.m_Destroyed && b.m_State.Awoken)
                    Call(b, b.m_State.Methods.OnApplicationQuit);
            try { Application.RaiseQuitting(); } catch (Exception e) { Debug.LogException(e); }
            foreach (MonoBehaviour b in snapshot)
            {
                if (b.m_Destroyed)
                    continue;
                if (b.m_State.EnabledActive)
                {
                    b.m_State.EnabledActive = false;
                    Call(b, b.m_State.Methods.OnDisable);
                }
            }
            foreach (MonoBehaviour b in snapshot)
            {
                if (b.m_Destroyed)
                    continue;
                if (b.m_State.Awoken)
                    Call(b, b.m_State.Methods.OnDestroy);
                b.OnEngineDestroy();
            }
            Release(snapshot);
            IsPlaying = false;
            ResetPlayState();
        }

        private static void ResetPlayState()
        {
            foreach (GameObject go in s_Objects.Values)
                go.m_Destroyed = true;
            s_Objects.Clear();
            s_Components.Clear();
            s_Behaviours.Clear();
            s_DestroyQueue.Clear();
            s_ActiveCache.Clear();
            CoroutineScheduler.Clear();
            InvokeScheduler.Clear();
        }

        private static void RunInitializers(RuntimeInitializeLoadType when)
        {
            foreach (MethodInfo method in ScriptDomain.InitializeMethods(when))
            {
                try
                {
                    method.Invoke(null, null);
                }
                catch (Exception e)
                {
                    Debug.LogException(e is TargetInvocationException tie && tie.InnerException != null ? tie.InnerException : e);
                }
            }
        }

        public static void RequestQuit() => s_QuitRequested = true;

        public static bool ConsumeQuitRequest()
        {
            bool q = s_QuitRequested;
            s_QuitRequested = false;
            return q;
        }

        // ================================================================ physics messages

        /// <summary>
        /// Sends OnCollision* / OnTrigger* for a batch of contacts from one physics step. Each side
        /// gets the message on the collider's object and, if different, on the object with its
        /// Rigidbody; disabled scripts receive them too, like in Unity.
        /// </summary>
        public static void DispatchContacts(ContactEvent* events, int count)
        {
            for (int i = 0; i < count; i++)
            {
                ContactEvent e = events[i];
                GameObject a = GetGameObject(e.A), b = GetGameObject(e.B);
                if (a == null || b == null)
                    continue;
                var point = new Vector3(e.PointX, e.PointY, e.PointZ);
                var normal = new Vector3(e.NormalX, e.NormalY, e.NormalZ);
                var velocity = new Vector3(e.VelocityX, e.VelocityY, e.VelocityZ);
                bool trigger = e.Trigger != 0;
                Deliver(a, b, trigger, e.Type, point, -normal, velocity);
                if (a != null && b != null)
                    Deliver(b, a, trigger, e.Type, point, normal, -velocity);
            }
        }

        private static readonly List<GameObject> s_ContactTargets = new List<GameObject>(2);

        private static void Deliver(GameObject self, GameObject other, bool trigger, int type, Vector3 point,
                                    Vector3 normal, Vector3 relativeVelocity)
        {
            s_ContactTargets.Clear();
            s_ContactTargets.Add(self);
            if (GetBuiltin(self, typeof(Rigidbody)) == null && self.transform.parent != null)
            {
                Rigidbody body = self.transform.parent.GetComponentInParent<Rigidbody>(true);
                if (body != null && body.gameObject != self)
                    s_ContactTargets.Add(body.gameObject);
            }

            Collider selfCollider = null, otherCollider = null;
            Collision collision = null;
            foreach (GameObject target in s_ContactTargets)
            {
                if (!s_Components.TryGetValue(target.m_Id, out List<Component> list))
                    continue;
                foreach (Component c in list.ToArray())
                {
                    if (!(c is MonoBehaviour mb) || mb.m_Destroyed || mb.m_State == null || !mb.m_State.Awoken)
                        continue;
                    MethodInfo method = mb.m_State.Methods.Contact[trigger ? 1 : 0, type];
                    if (method == null)
                        continue;
                    object[] args = null;
                    if (method.GetParameters().Length == 1)
                    {
                        otherCollider ??= other.GetComponent<Collider>();
                        if (trigger)
                        {
                            args = new object[] { otherCollider };
                        }
                        else
                        {
                            selfCollider ??= self.GetComponent<Collider>();
                            Rigidbody otherBody = otherCollider != null ? otherCollider.attachedRigidbody : null;
                            collision ??= new Collision
                            {
                                m_Other = otherBody != null ? otherBody.gameObject : other,
                                m_Collider = otherCollider,
                                m_RelativeVelocity = relativeVelocity,
                                m_Contacts = new[]
                                {
                                    new ContactPoint { m_Point = point, m_Normal = normal, m_This = selfCollider, m_Other = otherCollider },
                                },
                            };
                            args = new object[] { collision };
                        }
                    }
                    Call(mb, method, args);
                    if (!self || !other)
                        return;
                }
            }
        }

        // ================================================================ destruction

        public static void ScheduleDestroy(Object obj, float delay)
        {
            if (!obj)
                return;
            if (!IsPlaying)
            {
                DestroyNow(obj);
                return;
            }
            s_DestroyQueue.Add((obj, Time.time + MathF.Max(0f, delay)));
        }

        private static void ProcessDestroyQueue()
        {
            if (s_DestroyQueue.Count == 0)
                return;
            var due = new List<Object>();
            for (int i = s_DestroyQueue.Count - 1; i >= 0; i--)
            {
                if (s_DestroyQueue[i].time <= Time.time)
                {
                    due.Add(s_DestroyQueue[i].obj);
                    s_DestroyQueue.RemoveAt(i);
                }
            }
            for (int i = due.Count - 1; i >= 0; i--)
                DestroyNow(due[i]);
        }

        public static void DestroyNow(Object obj)
        {
            if (!obj)
                return;
            switch (obj)
            {
                case GameObject go:
                    DestroyGameObject(go.m_Id, true);
                    break;
                case Transform:
                    Debug.LogError("Can't destroy Transform component. If you want to destroy the game object, please call 'Destroy' on the game object instead.");
                    break;
                case MonoBehaviour b:
                    DestroyBehaviour(b, true);
                    break;
                case NativeComponent c:
                    Native.Api.ComponentRemove(c.m_GameObject.m_Id, (int)c.Kind);
                    c.OnEngineDestroy();
                    if (s_Components.TryGetValue(c.m_GameObject.m_Id, out List<Component> list))
                        list.Remove(c);
                    break;
                default:
                    InvokeMessage(obj, "OnDisable");
                    InvokeMessage(obj, "OnDestroy");
                    obj.OnEngineDestroy();
                    break;
            }
        }

        private static void DestroyBehaviour(MonoBehaviour b, bool removeFromList)
        {
            if (b.m_Destroyed)
                return;
            ScriptState s = b.m_State;
            if (s != null && s.EnabledActive)
            {
                s.EnabledActive = false;
                Call(b, s.Methods.OnDisable);
            }
            if (s != null && s.Awoken)
                Call(b, s.Methods.OnDestroy);
            CoroutineScheduler.StopAll(b);
            InvokeScheduler.Cancel(b, null);
            b.OnEngineDestroy();
            s_Behaviours.Remove(b);
            if (removeFromList && !(b.m_GameObject is null) && s_Components.TryGetValue(b.m_GameObject.m_Id, out List<Component> list))
                list.Remove(b);
        }

        /// <summary>Destroys an entity and its children, running OnDisable / OnDestroy first.</summary>
        public static void DestroyGameObject(uint rootId, bool destroyNative)
        {
            if (!EntityExists(rootId))
                return;
            uint[] ids = Subtree(rootId);
            var scripts = new List<MonoBehaviour>();
            foreach (uint id in ids)
                if (s_Components.TryGetValue(id, out List<Component> list))
                    foreach (Component c in list)
                        if (c is MonoBehaviour mb && !mb.m_Destroyed)
                            scripts.Add(mb);

            foreach (MonoBehaviour b in scripts)
            {
                if (b.m_State != null && b.m_State.EnabledActive)
                {
                    b.m_State.EnabledActive = false;
                    Call(b, b.m_State.Methods.OnDisable);
                }
            }
            foreach (MonoBehaviour b in scripts)
                DestroyBehaviour(b, false);

            foreach (uint id in ids)
            {
                if (s_Components.TryGetValue(id, out List<Component> list))
                {
                    foreach (Component c in list)
                        c.OnEngineDestroy();
                    s_Components.Remove(id);
                }
                if (s_Objects.TryGetValue(id, out GameObject go))
                {
                    go.OnEngineDestroy();
                    s_Objects.Remove(id);
                }
            }
            if (destroyNative && EntityExists(rootId))
                Native.Api.EntityDestroy(rootId);
        }

        // ================================================================ instantiation

        public static Object Instantiate(Object original, Transform parent, bool worldPositionStays,
                                         Vector3? position, Quaternion? rotation)
        {
            if (!original)
                throw new ArgumentException("The Object you want to instantiate is null.");

            if (original is ScriptableObject so)
            {
                var copy = (ScriptableObject)Activator.CreateInstance(so.GetType(), true);
                FieldCodec.ReadInto(copy, FieldCodec.WriteObject(so), null);
                copy.name = so.name + "(Clone)";
                return copy;
            }

            GameObject sourceGo = original switch
            {
                GameObject g => g,
                Component c => c.gameObject,
                _ => throw new ArgumentException($"Instantiating '{original.GetType().Name}' is not supported."),
            };

            uint parentId = parent != null ? parent.gameObject.m_Id : 0u;
            uint newRoot = Native.Api.EntityInstantiate(sourceGo.m_Id, parentId, worldPositionStays ? 1 : 0);
            if (newRoot == 0)
                return null;
            GameObject clone = GetGameObject(newRoot);
            clone.name = sourceGo.name + "(Clone)";
            if (position.HasValue || rotation.HasValue)
            {
                if (position.HasValue && rotation.HasValue)
                    clone.transform.SetPositionAndRotation(position.Value, rotation.Value);
                else if (position.HasValue)
                    clone.transform.position = position.Value;
                else
                    clone.transform.rotation = rotation.Value;
            }

            uint[] src = Subtree(sourceGo.m_Id);
            uint[] dst = Subtree(newRoot);
            var remap = new Dictionary<uint, uint>();
            for (int i = 0; i < src.Length && i < dst.Length; i++)
                remap[src[i]] = dst[i];

            var created = new List<(MonoBehaviour source, MonoBehaviour copy)>();
            for (int i = 0; i < src.Length && i < dst.Length; i++)
            {
                if (!s_Components.TryGetValue(src[i], out List<Component> list))
                    continue;
                GameObject target = GetGameObject(dst[i]);
                foreach (Component c in list.ToArray())
                {
                    if (c is MonoBehaviour mb && !mb.m_Destroyed)
                    {
                        MonoBehaviour copy = CreateBehaviour(target, mb.GetType(), null, mb.m_Enabled);
                        if (copy != null)
                            created.Add((mb, copy));
                    }
                }
            }
            foreach (var (source, copy) in created)
                FieldCodec.ReadInto(copy, FieldCodec.WriteObject(source), id => remap.TryGetValue(id, out uint m) ? m : id);

            if (IsPlaying)
            {
                foreach (var (_, copy) in created)
                    Activate(copy, IsActiveInHierarchy(copy.m_GameObject.m_Id));
            }

            if (original is GameObject)
                return clone;
            // Same component on the clone: same type, same position among the source's components of that type.
            var sourceList = new List<Component>();
            CollectComponents(sourceGo, original.GetType(), sourceList, false, true);
            int index = Math.Max(0, sourceList.IndexOf((Component)original));
            var cloneList = new List<Component>();
            CollectComponents(clone, original.GetType(), cloneList, false, true);
            return index < cloneList.Count ? cloneList[index] : null;
        }

        // ================================================================ messages

        public static void SendMessage(GameObject go, string methodName, object value, SendMessageOptions options,
                                       bool broadcast, bool upwards)
        {
            var targets = new List<GameObject>();
            if (broadcast)
            {
                foreach (uint id in Subtree(go.m_Id))
                    if (GetGameObject(id) is GameObject g && g.activeInHierarchy)
                        targets.Add(g);
            }
            else if (upwards)
            {
                for (Transform t = go.transform; t != null; t = t.parent)
                    targets.Add(t.gameObject);
            }
            else
            {
                targets.Add(go);
            }

            bool received = false;
            foreach (GameObject target in targets)
            {
                if (!s_Components.TryGetValue(target.m_Id, out List<Component> list))
                    continue;
                foreach (Component c in list.ToArray())
                {
                    if (!(c is MonoBehaviour mb) || mb.m_Destroyed)
                        continue;
                    MethodInfo method = FindMethod(mb.GetType(), methodName, 1) ?? FindMethod(mb.GetType(), methodName, 0);
                    if (method == null)
                        continue;
                    received = true;
                    Call(mb, method, method.GetParameters().Length == 1 ? new[] { value } : null);
                }
            }
            if (!received && options == SendMessageOptions.RequireReceiver)
                Debug.LogError($"SendMessage {methodName} has no receiver!");
        }

        // ================================================================ editor support

        public static List<MonoBehaviour> ScriptsOn(uint entity)
        {
            var result = new List<MonoBehaviour>();
            if (s_Components.TryGetValue(entity, out List<Component> list))
                foreach (Component c in list)
                    if (c is MonoBehaviour mb && !mb.m_Destroyed)
                        result.Add(mb);
            return result;
        }

        public static string DescribeException(Exception e) => DescribeException(e, true);

        /// <summary>Type, message and stack; for script errors the engine's own frames are left out.</summary>
        public static string DescribeException(Exception e, bool hideEngineFrames)
        {
            if (e is TargetInvocationException tie && tie.InnerException != null)
                e = tie.InnerException;
            var sb = new StringBuilder();
            sb.Append(e.GetType().Name).Append(": ").Append(e.Message);
            if (!string.IsNullOrEmpty(e.StackTrace))
            {
                foreach (string line in e.StackTrace.Split('\n'))
                {
                    string trimmed = line.Trim();
                    // Engine plumbing frames only add noise to a script error.
                    if (hideEngineFrames && (trimmed.Contains("IndeetsEngine.Runtime.") || trimmed.Contains("System.Reflection.") ||
                                             trimmed.Contains("System.RuntimeMethodHandle")))
                        continue;
                    sb.Append('\n').Append(trimmed);
                }
            }
            return sb.ToString();
        }
    }
}
