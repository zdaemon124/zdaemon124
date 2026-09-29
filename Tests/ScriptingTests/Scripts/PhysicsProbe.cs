using UnityEngine;

// A falling sphere and a raycast against the ground.
public class PhysicsProbe : MonoBehaviour
{
    private Rigidbody body;
    private float startY;
    private int frame;

    private void Start()
    {
        body = GetComponent<Rigidbody>();
        startY = transform.position.y;
        TestLog.Check("physics.rigidbody", body != null && !body.isKinematic);
        TestLog.Check("physics.collider", GetComponent<Collider>() is SphereCollider);
    }

    private void Update()
    {
        if (++frame != 30)
            return;
        TestLog.Check("physics.falls", transform.position.y < startY - 0.5f, $"{startY} -> {transform.position.y}");
        TestLog.Check("physics.velocity", body.velocity.y < -1f, body.velocity.ToString());
        bool hit = Physics.Raycast(new Vector3(0f, 5f, 3f), Vector3.down, out RaycastHit info, 20f);
        TestLog.Check("physics.raycast", hit && TestLog.Near(info.point.y, 0f, 0.05f) && info.transform != null && info.transform.name == "Ground",
            hit ? $"{info.point} {info.transform?.name}" : "no hit");
        body.velocity = Vector3.zero;
        transform.position = new Vector3(0f, 50f, 0f);
        TestLog.Check("physics.teleport", TestLog.Near(transform.position.y, 50f));
    }
}
