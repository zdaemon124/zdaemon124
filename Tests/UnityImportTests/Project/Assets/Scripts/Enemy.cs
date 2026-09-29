using UnityEngine;

public class Enemy : MonoBehaviour
{
    public EnemyConfig config;
    public int health = 10;
    public Transform weapon;
    public bool angry;
    public string title = "";

    public static int Awakened;

    [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.SubsystemRegistration)]
    private static void ResetStatics() => Awakened = 0;

    private void Awake() => Awakened++;
}
