using System.Collections.Generic;
using UnityEngine;

// Shared recorder: the C++ harness reads "CHECK <name> ok" / "FAIL <name> ..." lines from the log.
public static class TestLog
{
    public static readonly List<string> Events = new List<string>();

    public static void Event(string e) => Events.Add(e);

    public static void Check(string name, bool condition, string detail = "")
    {
        if (condition)
            Debug.Log($"CHECK {name} ok");
        else
            Debug.LogError($"FAIL {name} {detail}");
    }

    public static bool Near(Vector3 a, Vector3 b, float eps = 1e-3f) => (a - b).sqrMagnitude < eps * eps;
    public static bool Near(float a, float b, float eps = 1e-3f) => Mathf.Abs(a - b) < eps;

    [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.SubsystemRegistration)]
    private static void ResetStatics() => Events.Clear();

    [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.BeforeSceneLoad)]
    private static void BeforeScene() => Event("BeforeSceneLoad");

    [RuntimeInitializeOnLoadMethod]
    private static void AfterScene() => Event("AfterSceneLoad");
}
