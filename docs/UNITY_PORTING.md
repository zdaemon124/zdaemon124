# Перенос Unity-проекта в IndeetsEngine

Цель: чтобы игра, сделанная в Unity (URP, Netcode for GameObjects, uGUI + TextMeshPro, Input System), запускалась в IndeetsEngine и вела себя так же.

Ниже перечислено, что игра реально использует. Цифры получены разбором её рантайм-скриптов, без редакторских. Для каждой подсистемы указано, что уже есть в движке и чего не хватает. Порядок этапов определяют зависимости: пока не работают скрипты, ни одна игровая система не запустится.

## Что использует игра

| Подсистема Unity | Масштаб в проекте | В движке сейчас |
|---|---|---|
| C#-скрипты: `MonoBehaviour`, корутины, `ScriptableObject`, `[SerializeField]`, `RuntimeInitializeOnLoadMethod`, `GetComponent*`, `Find*` | ~370 скриптов, ~121 тыс. строк; `UnityEngine` — в 437 файлах | нет (этап 4) |
| Анимация: `Animator` со state machine, humanoid avatar, IK, blend shapes, `SkinnedMeshRenderer` | 33 скрипта, ~220 клипов `.anim`, 11 контроллеров | нет: модели импортируются статичными |
| Сеть: Netcode for GameObjects (`NetworkBehaviour`, `NetworkVariable`, `NetworkList`, `ClientRpc`/`ServerRpc`, `NetworkTransform`, транспорт UTP) | 23 сетевых компонента | нет |
| UI: uGUI (`Canvas`, `Image`, `Button`, layout, маски, `EventSystem`), TextMeshPro (SDF-текст), world-space плашки | 53 / 25 / 33 скрипта | базовые Image и Text, Rect Transform, Canvas Scaler |
| Рендер URP: 72 шейдера и Shader Graph, декали (`DecalProjector`), объёмный свет, вода, dither-fade через `MaterialPropertyBlock`, `RenderTexture` и дополнительные камеры для портретов | 10 скриптов с декалями, 5 с RT | Vulkan-рендер, Blinn-Phong, текстуры; нет MPB, декалей, RT-камер из скриптов, пользовательских шейдеров |
| Частицы: `ParticleSystem`, `TrailRenderer`, `LineRenderer` | 22 + 6 скриптов | нет |
| Физика: `CharacterController`, raycast, sphere cast, overlap (`NonAlloc`), слои, триггеры, регдоллы (суставы) | 25 скриптов с `CharacterController`, 11 с `Rigidbody` | Jolt: тела, коллайдеры, триггеры, raycast; нет `CharacterController`, overlap и sphere cast в API, суставов |
| Навигация: NavMesh + `NavMeshAgent` | 8 скриптов | нет |
| Ввод: Input System (`Keyboard.current`, `Mouse.current`), переназначение клавиш | 21 скрипт | `Input` (GLFW), легко отобразить |
| Ландшафт: `Terrain` со слоями текстур | ~16 terrain layers | нет |
| Ассеты: `.unity`/`.prefab` (YAML), `.mat`, `.meta` (GUID), `Resources.Load` | 3 сцены, ~200 префабов | свои `.zscene`/`.zprefab` (JSON), FBX/OBJ/glTF |

## Этапы

1. **C#-рантайм (.NET 8, hostfxr)** и слой совместимости `UnityEngine`: `GameObject`, `Transform`, `Component`, `MonoBehaviour` (Awake/OnEnable/Start/Update/LateUpdate/FixedUpdate/OnDisable/OnDestroy, порядок `DefaultExecutionOrder`), корутины (`yield return null`, `WaitForSeconds[Realtime]`, вложенные `IEnumerator`), `ScriptableObject`, `Time`, `Mathf`/`Vector3`/`Quaternion`/`Color`, `Debug`, `Resources`, `Object.Destroy`/`Instantiate`, «псевдо-null» у уничтоженных объектов (`obj == null`).
   Без этого не запустится ни одна игровая система, поэтому это первый шаг.
2. **Импорт Unity-проекта**: чтение YAML сцен и префабов, разрешение GUID из `.meta`, материалы `.mat` → материалы движка, сериализованные поля скриптов → поля C#-компонентов.
3. **Скиннинг и анимация**: `SkinnedMeshRenderer` (GPU-скиннинг), клипы из FBX и `.anim`, `Animator` (слои, состояния, переходы, параметры, avatar masks), humanoid-ретаргет, IK ног и головы, blend shapes.
4. **uGUI + SDF-текст**: `Canvas` (overlay и world space), `Image` (sliced, filled), `Button`/`Toggle`/`Slider`/`ScrollRect`, Layout Groups, `Mask`/`RectMask2D`, `EventSystem` (hover, click, drag), SDF-шрифты уровня TextMeshPro, rich text.
5. **Физика для персонажей**: `CharacterController` (capsule sweep, ступеньки, склоны) поверх Jolt `CharacterVirtual`, `SphereCast`/`Overlap*NonAlloc`, слои и маски, суставы и регдоллы.
6. **Сеть**: собственная репликация по образцу NGO (spawn/despawn, owner/server authority, `NetworkVariable`, RPC, синхронизация трансформов) поверх UDP-транспорта.
7. **Рендер**: `MaterialPropertyBlock`, пользовательские шейдеры (порт URP-шейдеров на GLSL), декали, RT-камеры, частицы, трейлы, прозрачность с render queue, вода, объёмный свет.
8. **NavMesh** (Recast/Detour) и `NavMeshAgent`, **ландшафт** со слоями, **аудио**.

Каждый этап проверяется на реальных сценах игры: сначала hublevel, затем SampleScene.
