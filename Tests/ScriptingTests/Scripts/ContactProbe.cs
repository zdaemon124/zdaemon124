using UnityEngine;

// A crate falls through a trigger zone onto the ground: collision and trigger messages.
public class ContactProbe : MonoBehaviour
{
    private int collisionEnters, collisionStays, triggerEnters, triggerExits, frame;
    private string groundName;
    private Vector3 normal, relativeVelocity;

    private void OnCollisionEnter(Collision collision)
    {
        collisionEnters++;
        groundName = collision.gameObject.name;
        normal = collision.GetContact(0).normal;
        relativeVelocity = collision.relativeVelocity;
    }

    private void OnCollisionStay(Collision collision) => collisionStays++;
    private void OnTriggerEnter(Collider other) { if (other.name == "Zone") triggerEnters++; }
    private void OnTriggerExit(Collider other) { if (other.name == "Zone") triggerExits++; }

    private void Update()
    {
        if (++frame != 85)
            return;
        TestLog.Check("contact.collisionEnter", collisionEnters == 1 && groundName == "Ground", $"{collisionEnters} {groundName}");
        TestLog.Check("contact.normal", normal.y > 0.9f, normal.ToString());
        TestLog.Check("contact.relativeVelocity", relativeVelocity.y > 1f, relativeVelocity.ToString());
        TestLog.Check("contact.collisionStay", collisionStays >= 3, collisionStays.ToString());
        TestLog.Check("contact.triggerEnterExit", triggerEnters == 1 && triggerExits == 1, $"{triggerEnters}/{triggerExits}");
        TestLog.Check("contact.zoneMessage", TriggerZone.entered == 1, TriggerZone.entered.ToString());
    }
}

public class TriggerZone : MonoBehaviour
{
    public static int entered;

    [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.SubsystemRegistration)]
    private static void ResetStatics() => entered = 0;

    // The parameterless form is valid too.
    private void OnTriggerEnter() => entered++;
}
