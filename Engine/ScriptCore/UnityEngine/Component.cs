using System;
using System.Collections;
using System.Collections.Generic;
using IndeetsEngine.Runtime;

namespace UnityEngine
{
    /// <summary>Base class for everything attached to a GameObject.</summary>
    public class Component : Object
    {
        internal GameObject m_GameObject;

        public GameObject gameObject => m_GameObject;
        public Transform transform => m_GameObject?.transform;

        public override string name
        {
            get => m_GameObject is null ? base.name : m_GameObject.name;
            set { if (!(m_GameObject is null)) m_GameObject.name = value; }
        }

        public string tag
        {
            get => m_GameObject?.tag;
            set { if (!(m_GameObject is null)) m_GameObject.tag = value; }
        }

        internal override bool IsAlive => !m_Destroyed && !(m_GameObject is null) && m_GameObject.IsAlive;

        public bool CompareTag(string tag) => m_GameObject != null && m_GameObject.CompareTag(tag);

        public T GetComponent<T>() => m_GameObject.GetComponent<T>();
        public Component GetComponent(Type type) => m_GameObject.GetComponent(type);
        public Component GetComponent(string type) => m_GameObject.GetComponent(type);
        public bool TryGetComponent<T>(out T component) => m_GameObject.TryGetComponent(out component);
        public bool TryGetComponent(Type type, out Component component) => m_GameObject.TryGetComponent(type, out component);
        public T[] GetComponents<T>() => m_GameObject.GetComponents<T>();
        public Component[] GetComponents(Type type) => m_GameObject.GetComponents(type);
        public void GetComponents<T>(List<T> results) => m_GameObject.GetComponents(results);
        public T GetComponentInChildren<T>() => m_GameObject.GetComponentInChildren<T>();
        public T GetComponentInChildren<T>(bool includeInactive) => m_GameObject.GetComponentInChildren<T>(includeInactive);
        public Component GetComponentInChildren(Type type, bool includeInactive = false) => m_GameObject.GetComponentInChildren(type, includeInactive);
        public T GetComponentInParent<T>() => m_GameObject.GetComponentInParent<T>();
        public T GetComponentInParent<T>(bool includeInactive) => m_GameObject.GetComponentInParent<T>(includeInactive);
        public Component GetComponentInParent(Type type, bool includeInactive = false) => m_GameObject.GetComponentInParent(type, includeInactive);
        public T[] GetComponentsInChildren<T>() => m_GameObject.GetComponentsInChildren<T>();
        public T[] GetComponentsInChildren<T>(bool includeInactive) => m_GameObject.GetComponentsInChildren<T>(includeInactive);
        public void GetComponentsInChildren<T>(List<T> results) => m_GameObject.GetComponentsInChildren(results);
        public void GetComponentsInChildren<T>(bool includeInactive, List<T> results) => m_GameObject.GetComponentsInChildren(includeInactive, results);
        public Component[] GetComponentsInChildren(Type type, bool includeInactive = false) => m_GameObject.GetComponentsInChildren(type, includeInactive);
        public T[] GetComponentsInParent<T>(bool includeInactive = false) => m_GameObject.GetComponentsInParent<T>(includeInactive);

        public void SendMessage(string methodName, object value = null, SendMessageOptions options = SendMessageOptions.RequireReceiver) =>
            m_GameObject.SendMessage(methodName, value, options);

        public void SendMessageUpwards(string methodName, object value = null, SendMessageOptions options = SendMessageOptions.RequireReceiver) =>
            m_GameObject.SendMessageUpwards(methodName, value, options);

        public void BroadcastMessage(string methodName, object parameter = null, SendMessageOptions options = SendMessageOptions.RequireReceiver) =>
            m_GameObject.BroadcastMessage(methodName, parameter, options);
    }

    /// <summary>A component that can be enabled or disabled.</summary>
    public class Behaviour : Component
    {
        internal bool m_Enabled = true;

        public bool enabled
        {
            get => m_Enabled;
            set
            {
                if (m_Enabled == value)
                    return;
                m_Enabled = value;
                OnEnabledChanged();
            }
        }

        public bool isActiveAndEnabled => m_Enabled && IsAlive && m_GameObject.activeInHierarchy;

        internal virtual void OnEnabledChanged() { }
    }

    /// <summary>Base class for scripts, as in Unity.</summary>
    public class MonoBehaviour : Behaviour
    {
        internal ScriptState m_State;

        public bool useGUILayout { get; set; } = true;

        internal override void OnEnabledChanged() => World.OnBehaviourEnabledChanged(this);

        public static void print(object message) => Debug.Log(message);

        // ---- Coroutines

        public Coroutine StartCoroutine(IEnumerator routine)
        {
            if (routine == null)
                throw new ArgumentNullException(nameof(routine));
            if (!isActiveAndEnabled && !(m_GameObject is null) && !m_GameObject.activeInHierarchy)
            {
                Debug.LogError($"Coroutine couldn't be started because the the game object '{name}' is inactive!", this);
                return null;
            }
            return CoroutineScheduler.Start(this, routine);
        }

        public Coroutine StartCoroutine(string methodName) => StartCoroutine(methodName, null);

        public Coroutine StartCoroutine(string methodName, object value)
        {
            var method = World.FindMethod(GetType(), methodName, value == null ? 0 : 1);
            if (method == null || !typeof(IEnumerator).IsAssignableFrom(method.ReturnType))
            {
                Debug.LogError($"Coroutine '{methodName}' couldn't be started!", this);
                return null;
            }
            var routine = (IEnumerator)method.Invoke(this, value == null ? null : new[] { value });
            Coroutine c = StartCoroutine(routine);
            if (c != null)
                c.m_MethodName = methodName;
            return c;
        }

        public void StopCoroutine(Coroutine routine) => CoroutineScheduler.Stop(this, routine);
        public void StopCoroutine(IEnumerator routine) => CoroutineScheduler.Stop(this, routine);
        public void StopCoroutine(string methodName) => CoroutineScheduler.Stop(this, methodName);
        public void StopAllCoroutines() => CoroutineScheduler.StopAll(this);

        // ---- Invoke

        public void Invoke(string methodName, float time) => InvokeScheduler.Add(this, methodName, time, -1f);
        public void InvokeRepeating(string methodName, float time, float repeatRate) => InvokeScheduler.Add(this, methodName, time, repeatRate);
        public void CancelInvoke() => InvokeScheduler.Cancel(this, null);
        public void CancelInvoke(string methodName) => InvokeScheduler.Cancel(this, methodName);
        public bool IsInvoking() => InvokeScheduler.IsInvoking(this, null);
        public bool IsInvoking(string methodName) => InvokeScheduler.IsInvoking(this, methodName);
    }

    // ---------------------------------------------------------------- yield instructions

    public class YieldInstruction { }

    public sealed class Coroutine : YieldInstruction
    {
        internal MonoBehaviour m_Owner;
        internal readonly Stack<IEnumerator> m_Stack = new Stack<IEnumerator>();
        internal IEnumerator m_Root;
        internal string m_MethodName;
        internal object m_Wait;
        internal float m_WaitUntil;
        internal bool m_Done;

        internal Coroutine() { }
    }

    public sealed class WaitForSeconds : YieldInstruction
    {
        internal readonly float m_Seconds;
        public WaitForSeconds(float seconds) { m_Seconds = seconds; }
    }

    public sealed class WaitForFixedUpdate : YieldInstruction { }

    public sealed class WaitForEndOfFrame : YieldInstruction { }

    public abstract class CustomYieldInstruction : IEnumerator
    {
        public abstract bool keepWaiting { get; }
        public object Current => null;
        public bool MoveNext() => keepWaiting;
        public virtual void Reset() { }
    }

    public class WaitForSecondsRealtime : CustomYieldInstruction
    {
        private float m_WaitUntilTime = -1f;

        public WaitForSecondsRealtime(float time) { waitTime = time; }

        public float waitTime { get; set; }

        public override bool keepWaiting
        {
            get
            {
                if (m_WaitUntilTime < 0f)
                    m_WaitUntilTime = Time.realtimeSinceStartup + waitTime;
                bool wait = Time.realtimeSinceStartup < m_WaitUntilTime;
                if (!wait)
                    m_WaitUntilTime = -1f;
                return wait;
            }
        }

        public override void Reset() => m_WaitUntilTime = -1f;
    }

    public sealed class WaitUntil : CustomYieldInstruction
    {
        private readonly Func<bool> m_Predicate;
        public WaitUntil(Func<bool> predicate) { m_Predicate = predicate; }
        public override bool keepWaiting => !m_Predicate();
    }

    public sealed class WaitWhile : CustomYieldInstruction
    {
        private readonly Func<bool> m_Predicate;
        public WaitWhile(Func<bool> predicate) { m_Predicate = predicate; }
        public override bool keepWaiting => m_Predicate();
    }

    public class AsyncOperation : YieldInstruction
    {
        public bool isDone { get; internal set; } = true;
        public float progress => isDone ? 1f : 0f;
        public bool allowSceneActivation { get; set; } = true;
        public int priority { get; set; }
        public event Action<AsyncOperation> completed
        {
            add => value?.Invoke(this);
            remove { }
        }
    }
}
