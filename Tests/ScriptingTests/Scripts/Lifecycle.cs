using System.Collections;
using UnityEngine;

// Scene object "Lifecycle": checks the order of Unity messages, coroutines, Invoke and fields.
[DefaultExecutionOrder(-100)]
public class Lifecycle : MonoBehaviour
{
    public float speed = 1f;                  // set to 5 by the scene
    [SerializeField] private int hidden = 3;  // set to 7 by the scene
    public Vector3 offset = Vector3.one;      // set to (1, 2, 3) by the scene
    public Color tint = Color.white;
    public GameObject target;                 // references the "Mover" entity
    public Mover mover;                       // same entity, as a component reference
    public string label = "default";
    public int[] numbers = { 1 };
    [System.NonSerialized] public int notSerialized = 11;

    private int updates;
    private int fixedUpdates;
    private int lateUpdates;
    private bool coroutineDone;
    private bool nestedDone;
    private bool waitUntilDone;
    private int invoked;
    private int repeating;

    private void Awake()
    {
        TestLog.Event("Lifecycle.Awake");
        TestLog.Check("fields.float", TestLog.Near(speed, 5f), $"speed={speed}");
        TestLog.Check("fields.private", hidden == 7, $"hidden={hidden}");
        TestLog.Check("fields.vector", TestLog.Near(offset, new Vector3(1, 2, 3)), offset.ToString());
        TestLog.Check("fields.color", TestLog.Near(tint.r, 0.5f), tint.ToString());
        TestLog.Check("fields.string", label == "from scene", label);
        TestLog.Check("fields.array", numbers != null && numbers.Length == 3 && numbers[2] == 30, numbers == null ? "null" : numbers.Length.ToString());
        TestLog.Check("fields.gameObject", target != null && target.name == "Mover", target == null ? "null" : target.name);
        TestLog.Check("fields.component", mover != null && mover.gameObject == target, mover == null ? "null" : mover.name);
        TestLog.Check("fields.nonSerialized", notSerialized == 11);
    }

    private void OnEnable() => TestLog.Event("Lifecycle.OnEnable");

    private IEnumerator Start()
    {
        TestLog.Event("Lifecycle.Start");
        TestLog.Check("start.afterAwakeOfAll", TestLog.Events.Contains("Mover.Awake"));
        Invoke(nameof(Invoked), 0.1f);
        InvokeRepeating(nameof(Repeating), 0f, 0.05f);
        StartCoroutine(Routine());

        // Start itself as a coroutine.
        int frame = Time.frameCount;
        yield return null;
        TestLog.Check("start.coroutine", Time.frameCount == frame + 1, $"{frame} -> {Time.frameCount}");
    }

    private IEnumerator Routine()
    {
        float t0 = Time.time;
        yield return new WaitForSeconds(0.2f);
        TestLog.Check("coroutine.waitForSeconds", Time.time - t0 >= 0.2f - 1e-4f, (Time.time - t0).ToString());
        yield return Nested();
        TestLog.Check("coroutine.nested", nestedDone);
        yield return new WaitUntil(() => updates > 20);
        waitUntilDone = true;
        yield return new WaitForFixedUpdate();
        TestLog.Check("coroutine.fixed", Time.inFixedTimeStep);
        yield return new WaitForEndOfFrame();
        coroutineDone = true;
    }

    private IEnumerator Nested()
    {
        yield return null;
        yield return null;
        nestedDone = true;
    }

    private void Invoked() => invoked++;
    private void Repeating() { repeating++; if (repeating == 3) CancelInvoke(nameof(Repeating)); }

    private void FixedUpdate() => fixedUpdates++;

    private void Update()
    {
        if (updates == 0)
        {
            TestLog.Event("Lifecycle.Update");
            string order = string.Join(",", TestLog.Events);
            int Pos(string e) => TestLog.Events.IndexOf(e);
            TestLog.Check("lifecycle.order",
                Pos("BeforeSceneLoad") >= 0 && Pos("BeforeSceneLoad") < Pos("Lifecycle.Awake") &&
                Pos("Lifecycle.Awake") < Pos("Lifecycle.OnEnable") && Pos("Lifecycle.OnEnable") < Pos("Mover.Awake") &&
                Pos("Mover.Awake") < Pos("AfterSceneLoad") && Pos("AfterSceneLoad") < Pos("Lifecycle.Start") &&
                Pos("Lifecycle.Start") < Pos("Lifecycle.Update"), order);
        }
        updates++;
        if (updates == 60)
        {
            TestLog.Check("fixedUpdate.ran", fixedUpdates > 30, fixedUpdates.ToString());
            TestLog.Check("lateUpdate.ran", lateUpdates == 59, lateUpdates.ToString());
            TestLog.Check("coroutine.done", coroutineDone && waitUntilDone);
            TestLog.Check("invoke.once", invoked == 1, invoked.ToString());
            TestLog.Check("invoke.repeating", repeating == 3, repeating.ToString());
            TestLog.Check("time.advances", Time.time > 0.9f && Time.frameCount == 60, $"{Time.time} {Time.frameCount}");
        }
    }

    private void LateUpdate() => lateUpdates++;

    private void OnDisable() => TestLog.Event("Lifecycle.OnDisable");
    private void OnDestroy()
    {
        TestLog.Event("Lifecycle.OnDestroy");
        TestLog.Check("lifecycle.exit", TestLog.Events.IndexOf("Lifecycle.OnDisable") >= 0 &&
                                        TestLog.Events.IndexOf("Lifecycle.OnDisable") < TestLog.Events.IndexOf("Lifecycle.OnDestroy"));
    }
}
