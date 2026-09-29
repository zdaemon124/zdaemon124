# C#-скрипты

Скрипты пишутся так же, как в Unity: класс, унаследованный от `MonoBehaviour`, в файле с тем же именем, `using UnityEngine;`. API повторяет UnityEngine, поэтому существующие Unity-скрипты компилируются без изменений в той части, которую движок уже поддерживает (список ниже).

```csharp
using UnityEngine;

public class Spinner : MonoBehaviour
{
    [Header("Motion")]
    [Tooltip("Degrees per second")]
    public float speed = 90f;
    [Range(0f, 2f)] public float bobHeight = 0.5f;
    [SerializeField] private GameObject target;

    void Update()
    {
        transform.Rotate(Vector3.up, speed * Time.deltaTime, Space.World);
    }

    void OnCollisionEnter(Collision collision)
    {
        Debug.Log($"Hit {collision.gameObject.name}");
    }
}
```

![Scripts](editor-scripts.png)

## Как это устроено

- **Среда .NET 8** запускается внутри движка через `hostfxr`. В релизной сборке она лежит в папке `dotnet` рядом с exe, ставить ничего не нужно. При сборке из исходников используется установленный .NET.
- **Компилятор C# (Roslyn) работает внутри редактора**, .NET SDK для скриптов не нужен. Сохраняете `.cs` в любом редакторе кода, и через секунду скрипты перекомпилируются. Ошибки с файлом и строкой появляются в Console. Пока есть ошибки, Play не запускается, как в Unity.
- **Горячая перезагрузка:** новая сборка скриптов подгружается без перезапуска редактора. Правки, сделанные во время Play, компилируются после Stop.
- Скрипты из папок `Editor` в сборку не попадают (это правило Unity).

## Работа в редакторе

- **Project → ПКМ → Create → C# Script** создаёт скрипт по шаблону Unity. Если сразу переименовать файл, класс переименуется вместе с ним. Двойной клик открывает файл в редакторе кода.
- Добавить скрипт на объект можно через **Add Component → Scripts** или перетаскиванием `.cs` из Project на объект в Hierarchy или в Inspector.
- В Inspector отображаются сериализуемые поля: `public` и `[SerializeField]`. Поддерживаются числа, bool, строки, enum (включая `[Flags]`), `Vector2/3/4`, `Quaternion` (в градусах), `Color`, `Rect`, `Bounds`, `LayerMask`, ссылки на `GameObject`/`Transform`/компоненты (выбор из списка или перетаскивание из Hierarchy), массивы, `List<T>` и вложенные `[Serializable]`-классы и структуры. Атрибуты `[Header]`, `[Tooltip]`, `[Range]`, `[Min]`, `[Space]`, `[TextArea]`, `[HideInInspector]`, `[FormerlySerializedAs]` работают.
- В Play Mode Inspector показывает живые значения работающих скриптов, и их можно менять. После Stop всё возвращается, как в Unity.
- Когда окно Game в фокусе, скрипты получают клавиатуру и мышь. `Esc` возвращает курсор, если скрипт его захватил.

## Жизненный цикл (как в Unity)

`Awake` → `OnEnable` для каждого объекта в порядке `[DefaultExecutionOrder]` → `Start` перед первым `Update`/`FixedUpdate` → каждый кадр: `FixedUpdate` + шаг физики + `OnCollision*/OnTrigger*` (столько раз, сколько фиксированных шагов) → `Update` → `Invoke` и корутины → `LateUpdate` → отложенный `Destroy` → при выходе `OnApplicationQuit`, `OnDisable`, `OnDestroy`.

Поддерживаются `[RuntimeInitializeOnLoadMethod]` (все варианты `RuntimeInitializeLoadType`), `SetActive` и `enabled` с вызовами `OnEnable`/`OnDisable`, остановка корутин при выключении объекта, исключения в скрипте (пишутся в Console со стеком, игра не падает), `Application.Quit()` (завершает Play Mode).

## Что уже есть в API

| Область | Классы и функции |
|---|---|
| Объекты | `Object` (уничтоженный объект равен `null`, как в Unity), `Destroy(obj, t)`, `DestroyImmediate`, `Instantiate` (все перегрузки; ссылки внутри копии переназначаются на копию), `FindObjectsByType`, `FindFirstObjectByType`, `FindAnyObjectByType`, `DontDestroyOnLoad` |
| GameObject | `new GameObject(name, types)`, `SetActive`, `activeSelf/activeInHierarchy`, `tag`, `layer`, `CompareTag`, `Find`, `FindWithTag`, `CreatePrimitive`, `GetComponent*` / `TryGetComponent` / `GetComponents*`, `AddComponent` (с `[RequireComponent]` и `[DisallowMultipleComponent]`), `SendMessage`, `BroadcastMessage` |
| Transform | `position/rotation/localPosition/localRotation/localScale/lossyScale`, `eulerAngles`, `forward/right/up`, `parent`, `SetParent`, `GetChild`, `childCount`, `Find("a/b")`, перебор `foreach (Transform child in transform)`, `Translate`, `Rotate`, `RotateAround`, `LookAt`, `TransformPoint/Direction/Vector` и обратные |
| MonoBehaviour | корутины (`yield return null`, `WaitForSeconds`, `WaitForSecondsRealtime`, `WaitForFixedUpdate`, `WaitForEndOfFrame`, `WaitUntil`, `WaitWhile`, вложенные `IEnumerator`, ожидание другой корутины, `CustomYieldInstruction`), `StopCoroutine`, `Invoke`, `InvokeRepeating`, `CancelInvoke`, `enabled`, `isActiveAndEnabled` |
| Физика | `Rigidbody` (`velocity`, `angularVelocity`, `mass`, `drag`, `useGravity`, `isKinematic`, `AddForce` со всеми `ForceMode`, `AddExplosionForce`, `MovePosition`), `BoxCollider`/`SphereCollider`/`CapsuleCollider`, `Physics.Raycast`/`RaycastAll`/`RaycastNonAlloc`/`Linecast`, `OnCollisionEnter/Stay/Exit(Collision)`, `OnTriggerEnter/Stay/Exit(Collider)` |
| Рендер | `Renderer.material.color`, `MaterialPropertyBlock` (цвет), `Light` (`color`, `intensity`), `Camera` (`main`, `fieldOfView`, `ScreenPointToRay`, `WorldToScreenPoint`, `ViewportToWorldPoint`...) |
| Математика | `Vector2/3/4`, `Vector2Int/3Int`, `Quaternion` (Euler в порядке Unity Z-X-Y, `LookRotation`, `Slerp`, `RotateTowards`...), `Mathf` (включая `SmoothDamp`, `PerlinNoise`), `Color`, `Color32`, `ColorUtility`, `Random`, `Ray`, `Bounds`, `Rect`, `LayerMask` |
| Ввод | `Input` (`GetKey*`, `GetMouseButton*`, `mousePosition`, `GetAxis("Horizontal"/"Vertical"/"Mouse X"...)`, `GetButton*`) и Input System: `Keyboard.current[Key.W].isPressed`, `wasPressedThisFrame`, `Mouse.current.position/delta/scroll/leftButton` |
| Ассеты | `Resources.Load`/`LoadAll`/`LoadAsync` (префабы, `ScriptableObject`, `TextAsset`, `Texture2D`, `Sprite`, `Material`, `AudioClip`), ссылки на префабы и ассеты в полях, `Instantiate(prefab)`; каждый ассет загружается один раз и общий для всех ссылок, как в Unity |
| Прочее | `Time` (`deltaTime`, `timeScale`, `fixedDeltaTime`, `unscaled*`, `frameCount`, `realtimeSinceStartup`), `Debug.Log*`, `Application`, `Screen`, `Cursor.lockState`, `PlayerPrefs` (JSON в `persistentDataPath`) |

## Чего пока нет

Эти части появятся со следующими этапами (см. [UNITY_PORTING.md](UNITY_PORTING.md)):

- пиксели текстур из ассетов (`Texture2D.GetPixel` работает только для текстур, созданных скриптом), воспроизведение `AudioClip`;
- `Animator`, uGUI/TextMeshPro, `NavMeshAgent`, Netcode, аудио, частицы;
- `Physics.SphereCast`/`OverlapSphere`, слои в физических запросах, `CharacterController`.

## Ассеты из скриптов

Поля с префабами, `ScriptableObject`, материалами, спрайтами и текстами, а также `Resources.Load`, работают так же, как в Unity.

- **Префаб** загружается в скрытый неактивный контейнер. Его скрипты создаются, но `Awake` у них не вызывается. `Find*` его не видит. `Instantiate(prefab)` делает копию в сцене, и у копии скрипты запускаются обычным образом. После Stop контейнер удаляется.
- **`ScriptableObject`** (`.asset`) создаётся один раз: поля читаются из файла, затем вызываются `Awake` и `OnEnable`. Ссылки между ассетами, в том числе циклические, сохраняются.
- **`Resources`** ищет ассеты в любых папках `Resources` по пути без расширения, например `Resources.Load<EnemyConfig>("Configs/Goblin")`. `LoadAll` берёт всё из папки, включая вложенные.

Движок читает Unity-файлы (`.prefab`, `.asset`, `.meta`) и свои (`.zprefab`, модели), поэтому папку `Assets` Unity-проекта можно открыть в редакторе как проект.

## Тесты

`Tests/ScriptingTests` компилирует настоящие C#-скрипты и прогоняет сцену без окна. Проверяются жизненный цикл, корутины, `Instantiate`/`Destroy`, физика с контактами, математика трансформов и горячая перезагрузка. Тест запускается через `ctest` локально и в CI на Windows и Linux.

`Tests/UnityImportTests` импортирует небольшой Unity-проект (`Tests/UnityImportTests/Project`: сцена, префаб, `ScriptableObject`, `Resources`) и запускает его скрипты. Проверяются переопределения в экземпляре префаба, ссылки в полях, `Resources.Load`, `Instantiate(prefab)`, теги и слои.
