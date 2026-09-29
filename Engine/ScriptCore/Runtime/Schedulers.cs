using System;
using System.Collections;
using System.Collections.Generic;
using System.Reflection;
using UnityEngine;

namespace IndeetsEngine.Runtime
{
    /// <summary>
    /// Unity-style coroutines. A coroutine runs to its first yield inside StartCoroutine, then
    /// resumes according to what it yielded: null (next frame, after Update), WaitForSeconds
    /// (scaled time), WaitForFixedUpdate, WaitForEndOfFrame, a nested IEnumerator (runs inline),
    /// another Coroutine (waits for it), or any CustomYieldInstruction.
    /// </summary>
    internal static class CoroutineScheduler
    {
        public enum Phase { Update, FixedUpdate, EndOfFrame }

        private sealed class NextFrame
        {
            public int Frame;
        }

        private static readonly List<Coroutine> s_All = new List<Coroutine>();
        private static readonly List<Coroutine> s_Tick = new List<Coroutine>();

        public static Coroutine Start(MonoBehaviour owner, IEnumerator routine)
        {
            var c = new Coroutine { m_Owner = owner, m_Root = routine };
            c.m_Stack.Push(routine);
            s_All.Add(c);
            Step(c);
            return c;
        }

        private static void Finish(Coroutine c)
        {
            c.m_Done = true;
            c.m_Stack.Clear();
            c.m_Wait = null;
        }

        private static void Step(Coroutine c)
        {
            // Guard against a coroutine that never yields anything that waits.
            for (int guard = 0; guard < 100000; guard++)
            {
                if (c.m_Done || c.m_Stack.Count == 0)
                {
                    Finish(c);
                    return;
                }
                IEnumerator top = c.m_Stack.Peek();
                bool moved;
                try
                {
                    moved = top.MoveNext();
                }
                catch (Exception e)
                {
                    Debug.LogException(e);
                    Finish(c);
                    return;
                }
                if (c.m_Done)
                    return; // stopped from inside the routine
                if (!moved)
                {
                    c.m_Stack.Pop();
                    continue;
                }

                object y = top.Current;
                switch (y)
                {
                    case null:
                        c.m_Wait = new NextFrame { Frame = Time.frameCount };
                        return;
                    case WaitForSeconds w:
                        c.m_Wait = w;
                        c.m_WaitUntil = Time.time + w.m_Seconds;
                        return;
                    case WaitForFixedUpdate:
                    case WaitForEndOfFrame:
                        c.m_Wait = y;
                        return;
                    case Coroutine other:
                        if (other.m_Done)
                            continue;
                        c.m_Wait = other;
                        return;
                    case CustomYieldInstruction custom:
                        if (!custom.keepWaiting)
                            continue;
                        c.m_Wait = custom;
                        return;
                    case IEnumerator nested:
                        c.m_Stack.Push(nested);
                        continue;
                    case AsyncOperation op:
                        if (op.isDone)
                            continue;
                        c.m_Wait = op;
                        return;
                    default:
                        c.m_Wait = new NextFrame { Frame = Time.frameCount };
                        return;
                }
            }
            Debug.LogError("Coroutine ran 100000 steps without waiting; it was stopped to keep the game responsive.");
            Finish(c);
        }

        private static bool Ready(Coroutine c, Phase phase)
        {
            switch (c.m_Wait)
            {
                case NextFrame n:
                    return phase == Phase.Update && Time.frameCount > n.Frame;
                case WaitForSeconds:
                    return phase == Phase.Update && Time.time >= c.m_WaitUntil;
                case WaitForFixedUpdate:
                    return phase == Phase.FixedUpdate;
                case WaitForEndOfFrame:
                    return phase == Phase.EndOfFrame;
                case Coroutine other:
                    return other.m_Done;
                case CustomYieldInstruction custom:
                    if (phase != Phase.Update)
                        return false;
                    try
                    {
                        return !custom.keepWaiting;
                    }
                    catch (Exception e)
                    {
                        Debug.LogException(e);
                        Finish(c);
                        return false;
                    }
                case AsyncOperation op:
                    return op.isDone;
                default:
                    return phase == Phase.Update;
            }
        }

        public static void Tick(Phase phase)
        {
            if (s_All.Count == 0)
                return;
            s_Tick.Clear();
            s_Tick.AddRange(s_All);
            foreach (Coroutine c in s_Tick)
            {
                if (c.m_Done)
                    continue;
                if (!c.m_Owner)
                {
                    Finish(c);
                    continue;
                }
                if (Ready(c, phase) && !c.m_Done)
                {
                    c.m_Wait = null;
                    Step(c);
                }
            }
            s_Tick.Clear();
            s_All.RemoveAll(c => c.m_Done);
        }

        public static void Stop(MonoBehaviour owner, Coroutine routine)
        {
            if (routine != null && routine.m_Owner == owner)
                Finish(routine);
        }

        public static void Stop(MonoBehaviour owner, IEnumerator routine)
        {
            foreach (Coroutine c in s_All)
                if (c.m_Owner == owner && !c.m_Done && ReferenceEquals(c.m_Root, routine))
                    Finish(c);
        }

        public static void Stop(MonoBehaviour owner, string methodName)
        {
            foreach (Coroutine c in s_All)
                if (c.m_Owner == owner && !c.m_Done && c.m_MethodName == methodName)
                    Finish(c);
        }

        public static void StopAll(MonoBehaviour owner)
        {
            foreach (Coroutine c in s_All)
                if (ReferenceEquals(c.m_Owner, owner))
                    Finish(c);
        }

        public static void Clear()
        {
            foreach (Coroutine c in s_All)
                Finish(c);
            s_All.Clear();
        }
    }

    /// <summary>MonoBehaviour.Invoke / InvokeRepeating.</summary>
    internal static class InvokeScheduler
    {
        private sealed class Entry
        {
            public MonoBehaviour Owner;
            public string Name;
            public MethodInfo Method;
            public float Time;
            public float Repeat;
            public bool Cancelled;
        }

        private static readonly List<Entry> s_Entries = new List<Entry>();

        public static void Add(MonoBehaviour owner, string methodName, float delay, float repeatRate)
        {
            MethodInfo method = World.FindMethod(owner.GetType(), methodName, 0);
            if (method == null)
            {
                Debug.LogError($"Trying to Invoke method: {owner.GetType().Name}.{methodName} couldn't be called.");
                return;
            }
            s_Entries.Add(new Entry
            {
                Owner = owner,
                Name = methodName,
                Method = method,
                Time = UnityEngine.Time.time + MathF.Max(0f, delay),
                Repeat = repeatRate,
            });
        }

        public static void Tick()
        {
            if (s_Entries.Count == 0)
                return;
            foreach (Entry e in s_Entries.ToArray())
            {
                if (e.Cancelled)
                    continue;
                if (!e.Owner)
                {
                    e.Cancelled = true;
                    continue;
                }
                // Repeating invokes catch up at most once per frame, like Unity.
                if (UnityEngine.Time.time < e.Time)
                    continue;
                if (e.Repeat > 0f)
                    e.Time += e.Repeat;
                else
                    e.Cancelled = true;
                World.Call(e.Owner, e.Method);
            }
            s_Entries.RemoveAll(e => e.Cancelled);
        }

        public static void Cancel(MonoBehaviour owner, string methodName)
        {
            foreach (Entry e in s_Entries)
                if (ReferenceEquals(e.Owner, owner) && (methodName == null || e.Name == methodName))
                    e.Cancelled = true;
        }

        public static bool IsInvoking(MonoBehaviour owner, string methodName)
        {
            foreach (Entry e in s_Entries)
                if (!e.Cancelled && ReferenceEquals(e.Owner, owner) && (methodName == null || e.Name == methodName))
                    return true;
            return false;
        }

        public static void Clear() => s_Entries.Clear();
    }
}
