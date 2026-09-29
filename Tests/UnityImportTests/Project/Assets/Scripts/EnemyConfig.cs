using System.Collections.Generic;
using UnityEngine;

[CreateAssetMenu(menuName = "Test/Enemy Config")]
public class EnemyConfig : ScriptableObject
{
    public string displayName;
    public float speed;
    public Color tint = Color.white;
    public List<int> drops = new List<int>();
    public EnemyConfig upgrade;
    public GameObject prefab;
    public bool boss;
}
