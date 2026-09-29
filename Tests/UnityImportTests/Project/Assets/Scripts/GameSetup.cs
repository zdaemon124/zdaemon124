using UnityEngine;

// Checks what a Unity project needs at runtime: prefab assets, ScriptableObjects, Resources,
// prefab instance overrides, tags and layers. The C++ harness reads the "CHECK <name> ok" lines.
public class GameSetup : MonoBehaviour
{
    public GameObject enemyPrefab;
    public EnemyConfig defaultConfig;
    public Enemy placed;
    public Vector3 spawnPoint;

    private static void Check(string name, bool condition)
    {
        if (condition)
            Debug.Log($"CHECK {name} ok");
        else
            Debug.LogError($"FAIL {name}");
    }

    private static bool Near(Vector3 a, Vector3 b) => (a - b).sqrMagnitude < 1e-4f;

    private void Start()
    {
        Check("prefab.reference", enemyPrefab != null && enemyPrefab.name == "Enemy");
        Check("prefab.assetNotAwake", Enemy.Awakened == 1); // only the instance placed in the scene
        Check("prefab.assetHidden", GameObject.Find("Enemy") == null && FindObjectsOfType<Enemy>().Length == 1);
        Check("prefab.assetNoParent", enemyPrefab.transform.parent == null);

        GameObject spawned = Instantiate(enemyPrefab, spawnPoint, Quaternion.identity);
        var enemy = spawned.GetComponent<Enemy>();
        Enemy source = enemyPrefab.GetComponent<Enemy>();
        Check("instantiate.awake", Enemy.Awakened == 2 && spawned.activeInHierarchy);
        Check("instantiate.fields", enemy != null && enemy.health == 10 && enemy.title == "Grunt" && enemy.config == defaultConfig);
        Check("instantiate.localReference", enemy.weapon != null && enemy.weapon.parent == spawned.transform && enemy.weapon != source.weapon);
        Check("instantiate.position", Near(spawned.transform.position, spawnPoint));
        Check("instantiate.found", FindObjectsOfType<Enemy>().Length == 2);

        Check("placed.overrides", placed != null && placed.health == 50 && placed.angry && placed.name == "Placed Enemy");
        Check("placed.transform", Near(placed.transform.position, new Vector3(5f, 0f, 2f)));

        var goblin = Resources.Load<EnemyConfig>("Configs/Goblin");
        Check("resources.scriptable", goblin != null && goblin == defaultConfig && goblin.name == "Goblin");
        Check("resources.scriptableFields", goblin.displayName == "Goblin" && Mathf.Approximately(goblin.speed, 3.5f) &&
                                            goblin.drops.Count == 3 && goblin.drops[2] == 7 && !goblin.boss &&
                                            Mathf.Approximately(goblin.tint.g, 0.5f));
        Check("resources.scriptableReferences", goblin.upgrade != null && goblin.upgrade.displayName == "Orc" &&
                                                goblin.upgrade.boss && goblin.upgrade.upgrade == goblin);
        Check("resources.scriptablePrefab", goblin.prefab == enemyPrefab);
        Check("resources.loadAll", Resources.LoadAll<EnemyConfig>("Configs").Length == 2);
        Check("resources.prefab", Resources.Load<GameObject>("Enemy") == enemyPrefab);
        var component = Resources.Load<Enemy>("Enemy");
        Check("resources.component", component != null && component.gameObject == enemyPrefab);
        var text = Resources.Load<TextAsset>("Data/names");
        Check("resources.text", text != null && text.text.StartsWith("alpha"));
        Check("resources.missing", Resources.Load("Nope") == null && Resources.Load<Texture2D>("Configs/Goblin") == null);
        ResourceRequest request = Resources.LoadAsync<EnemyConfig>("Configs/Orc");
        Check("resources.async", request.isDone && request.asset == goblin.upgrade);

        EnemyConfig copy = Instantiate(goblin);
        Check("instantiate.scriptable", copy != goblin && copy.displayName == "Goblin" && copy.upgrade == goblin.upgrade);

        Check("tag.layer", CompareTag("GameController") && gameObject.layer == 5);
        Check("tag.find", GameObject.FindWithTag("Player") != null && GameObject.FindWithTag("Player").name == "Ground");
        gameObject.tag = "Finish";
        Check("tag.set", CompareTag("Finish") && gameObject.tag == "Finish");
    }
}
