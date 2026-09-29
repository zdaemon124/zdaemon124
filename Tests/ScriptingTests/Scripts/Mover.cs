using UnityEngine;

public class Mover : MonoBehaviour
{
    public Vector3 velocity = new Vector3(1f, 0f, 0f);

    private void Awake() => TestLog.Event("Mover.Awake");

    private void Update() => transform.position += velocity * Time.deltaTime;
}
