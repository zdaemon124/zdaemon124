using UnityEngine;

// Instantiate / Destroy / fake null / AddComponent / SetActive / GetComponent on built-ins.
public class Spawner : MonoBehaviour
{
    public GameObject template;   // "Template" entity with a Counter script and a child
    private GameObject clone;
    private GameObject doomed;
    private Counter added;
    private int frame;
    private int onDisableBefore;

    private void Start()
    {
        clone = Instantiate(template, new Vector3(10f, 0f, 0f), Quaternion.identity);
        TestLog.Check("instantiate.name", clone.name == "Template(Clone)", clone.name);
        TestLog.Check("instantiate.position", TestLog.Near(clone.transform.position, new Vector3(10f, 0f, 0f)));
        var counter = clone.GetComponent<Counter>();
        TestLog.Check("instantiate.script", counter != null && counter.awoken, counter == null ? "null" : "not awoken");
        TestLog.Check("instantiate.fieldsCopied", counter != null && counter.step == 4, counter == null ? "" : counter.step.ToString());
        TestLog.Check("instantiate.refRemapped", counter != null && counter.child != null && counter.child.transform.parent == clone.transform,
            counter == null || counter.child == null ? "null" : counter.child.transform.parent?.name);
        TestLog.Check("instantiate.child", clone.transform.childCount == 1 && clone.transform.GetChild(0).name == "Child");

        doomed = new GameObject("Doomed");
        added = doomed.AddComponent<Counter>();
        TestLog.Check("addComponent.awake", added.awoken);
        TestLog.Check("addComponent.notStartedYet", !added.started);
        Destroy(doomed, 0.25f);

        var body = GetComponent<Rigidbody>();
        TestLog.Check("getComponent.missing", body == null);
        var rb = gameObject.AddComponent<Rigidbody>();
        TestLog.Check("addComponent.builtin", rb != null && GetComponent<Rigidbody>() == rb);
        rb.isKinematic = true;
        TestLog.Check("builtin.property", rb.isKinematic);

        var found = FindObjectsByType<Counter>(FindObjectsSortMode.None);
        TestLog.Check("findObjects", found.Length == 3, found.Length.ToString()); // template, clone, doomed
    }

    private void Update()
    {
        frame++;
        if (frame == 2)
            TestLog.Check("addComponent.startedNextFrame", added.started);
        if (frame == 5)
        {
            onDisableBefore = Counter.disabled;
            int enabledBefore = clone.GetComponent<Counter>().enabledCount;
            clone.SetActive(false);
            TestLog.Check("setActive.onDisable", Counter.disabled == onDisableBefore + 1, $"{Counter.disabled}");
            TestLog.Check("setActive.activeInHierarchy", !clone.GetComponent<Counter>().isActiveAndEnabled);
            clone.SetActive(true);
            TestLog.Check("setActive.onEnable", enabledBefore == 1 && clone.GetComponent<Counter>().enabledCount == 2,
                $"{enabledBefore} -> {clone.GetComponent<Counter>().enabledCount}");
        }
        if (frame == 40)
        {
            TestLog.Check("destroy.delayed", doomed == null && added == null);
            TestLog.Check("destroy.fakeNullIsNotReferenceNull", !ReferenceEquals(doomed, null));
            TestLog.Check("destroy.onDestroy", Counter.destroyed == 1, Counter.destroyed.ToString());
            Destroy(clone);
            TestLog.Check("destroy.endOfFrame", clone != null);
        }
        if (frame == 41)
            TestLog.Check("destroy.gone", clone == null && GameObject.Find("Template(Clone)") == null);
        if (frame == 45)
            throw new System.InvalidOperationException("expected test exception");
    }
}

public class Counter : MonoBehaviour
{
    public static int disabled;
    public static int destroyed;
    public int step = 1;
    public GameObject child;
    // Runtime state, not serialized: Instantiate must not copy it (public fields would be).
    [System.NonSerialized] public bool awoken;
    [System.NonSerialized] public bool started;
    [System.NonSerialized] public int enabledCount;

    [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.SubsystemRegistration)]
    private static void ResetStatics() { disabled = 0; destroyed = 0; }

    private void Awake() => awoken = true;
    private void OnEnable() => enabledCount++;
    private void Start() => started = true;
    private void OnDisable() => disabled++;
    private void OnDestroy() => destroyed++;
}
