using UnityEngine;

// Unity math conventions (left-handed, Euler Z-X-Y).
public class MathChecks : MonoBehaviour
{
    private void Start()
    {
        TestLog.Check("math.eulerForward", TestLog.Near(Quaternion.Euler(90f, 0f, 0f) * Vector3.forward, Vector3.down));
        TestLog.Check("math.eulerRight", TestLog.Near(Quaternion.Euler(0f, 90f, 0f) * Vector3.forward, Vector3.right));
        Vector3 e = new Vector3(30f, 45f, 60f);
        TestLog.Check("math.eulerRoundTrip", TestLog.Near(Quaternion.Euler(e).eulerAngles, e, 1e-2f), Quaternion.Euler(e).eulerAngles.ToString());
        Quaternion look = Quaternion.LookRotation(new Vector3(1f, 0f, 1f));
        TestLog.Check("math.lookRotation", TestLog.Near(look.eulerAngles.y, 45f, 1e-2f), look.eulerAngles.ToString());
        TestLog.Check("math.cross", Vector3.Cross(Vector3.up, Vector3.forward) == Vector3.right);

        // Parent/child world transforms.
        var parent = new GameObject("MathParent").transform;
        var child = new GameObject("MathChild").transform;
        parent.position = new Vector3(1f, 0f, 0f);
        parent.rotation = Quaternion.Euler(0f, 90f, 0f);
        child.SetParent(parent, false);
        child.localPosition = new Vector3(0f, 0f, 2f);
        TestLog.Check("transform.childWorld", TestLog.Near(child.position, new Vector3(3f, 0f, 0f)), child.position.ToString());
        child.position = new Vector3(1f, 1f, 0f);
        TestLog.Check("transform.setWorld", TestLog.Near(child.localPosition, new Vector3(0f, 1f, 0f)), child.localPosition.ToString());
        TestLog.Check("transform.inverse", TestLog.Near(parent.InverseTransformPoint(parent.TransformPoint(new Vector3(1, 2, 3))), new Vector3(1, 2, 3)));
        TestLog.Check("transform.find", parent.Find("MathChild") == child && GameObject.Find("MathParent/MathChild") == child.gameObject);
        child.SetParent(null, true);
        TestLog.Check("transform.unparentKeepsWorld", TestLog.Near(child.position, new Vector3(1f, 1f, 0f)));
    }
}
