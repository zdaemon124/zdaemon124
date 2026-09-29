using System;
using System.Collections.Generic;
using IndeetsEngine.Interop;
using IndeetsEngine.Runtime;

namespace UnityEngine
{
    /// <summary>Common plumbing for components whose data lives in the native engine.</summary>
    public abstract unsafe class NativeComponent : Component
    {
        internal abstract BuiltinKind Kind { get; }

        internal override bool IsAlive =>
            !m_Destroyed && !(m_GameObject is null) && m_GameObject.IsAlive &&
            Native.Api.ComponentHas(m_GameObject.m_Id, (int)Kind) != 0 && MatchesNative();

        /// <summary>Subclasses sharing one native component (the collider shapes) narrow this.</summary>
        internal virtual bool MatchesNative() => true;

        internal unsafe float GetFloat(int prop)
        {
            float* v = stackalloc float[4];
            Native.Api.ComponentGet(m_GameObject.m_Id, (int)Kind, prop, v);
            return v[0];
        }

        internal unsafe void SetFloat(int prop, float value)
        {
            float* v = stackalloc float[4];
            v[0] = value;
            Native.Api.ComponentSet(m_GameObject.m_Id, (int)Kind, prop, v);
        }

        internal bool GetBool(int prop) => GetFloat(prop) != 0f;
        internal void SetBool(int prop, bool value) => SetFloat(prop, value ? 1f : 0f);

        internal unsafe Vector3 GetVector3(int prop)
        {
            float* v = stackalloc float[4];
            Native.Api.ComponentGet(m_GameObject.m_Id, (int)Kind, prop, v);
            return new Vector3(v[0], v[1], v[2]);
        }

        internal unsafe void SetVector3(int prop, Vector3 value)
        {
            float* v = stackalloc float[4];
            v[0] = value.x; v[1] = value.y; v[2] = value.z;
            Native.Api.ComponentSet(m_GameObject.m_Id, (int)Kind, prop, v);
        }

        internal unsafe Color GetColor(int prop)
        {
            float* v = stackalloc float[4];
            v[3] = 1f;
            Native.Api.ComponentGet(m_GameObject.m_Id, (int)Kind, prop, v);
            return new Color(v[0], v[1], v[2], v[3]);
        }

        internal unsafe void SetColor(int prop, Color value)
        {
            float* v = stackalloc float[4];
            v[0] = value.r; v[1] = value.g; v[2] = value.b; v[3] = value.a;
            Native.Api.ComponentSet(m_GameObject.m_Id, (int)Kind, prop, v);
        }

        internal unsafe string GetString(int prop) =>
            Native.FromUtf8(Native.Api.ComponentGetString(m_GameObject.m_Id, (int)Kind, prop));

        internal unsafe void SetString(int prop, string value)
        {
            fixed (byte* p = Native.Utf8(value))
                Native.Api.ComponentSetString(m_GameObject.m_Id, (int)Kind, prop, p);
        }
    }

    // ------------------------------------------------------------------ physics

    public enum ForceMode { Force = 0, Impulse = 1, VelocityChange = 2, Acceleration = 5 }

    public enum RigidbodyInterpolation { None = 0, Interpolate = 1, Extrapolate = 2 }

    public enum CollisionDetectionMode { Discrete = 0, Continuous = 1, ContinuousDynamic = 2, ContinuousSpeculative = 3 }

    [Flags]
    public enum RigidbodyConstraints
    {
        None = 0,
        FreezePositionX = 2, FreezePositionY = 4, FreezePositionZ = 8,
        FreezeRotationX = 16, FreezeRotationY = 32, FreezeRotationZ = 64,
        FreezePosition = 14, FreezeRotation = 112, FreezeAll = 126,
    }

    public sealed class Rigidbody : NativeComponent
    {
        private const int PropMass = 0, PropLinearDamping = 1, PropAngularDamping = 2, PropUseGravity = 3,
            PropIsKinematic = 4, PropVelocity = 5, PropAngularVelocity = 6;

        internal override BuiltinKind Kind => BuiltinKind.Rigidbody;

        public float mass { get => GetFloat(PropMass); set => SetFloat(PropMass, value); }
        public float linearDamping { get => GetFloat(PropLinearDamping); set => SetFloat(PropLinearDamping, value); }
        public float angularDamping { get => GetFloat(PropAngularDamping); set => SetFloat(PropAngularDamping, value); }
        public float drag { get => linearDamping; set => linearDamping = value; }
        public float angularDrag { get => angularDamping; set => angularDamping = value; }
        public bool useGravity { get => GetBool(PropUseGravity); set => SetBool(PropUseGravity, value); }
        public bool isKinematic { get => GetBool(PropIsKinematic); set => SetBool(PropIsKinematic, value); }
        public Vector3 linearVelocity { get => GetVector3(PropVelocity); set => SetVector3(PropVelocity, value); }
        public Vector3 velocity { get => linearVelocity; set => linearVelocity = value; }
        public Vector3 angularVelocity { get => GetVector3(PropAngularVelocity); set => SetVector3(PropAngularVelocity, value); }

        public RigidbodyInterpolation interpolation { get; set; }
        public CollisionDetectionMode collisionDetectionMode { get; set; }
        public RigidbodyConstraints constraints { get; set; }
        public bool detectCollisions { get; set; } = true;
        public float maxAngularVelocity { get; set; } = 7f;
        public float sleepThreshold { get; set; } = 0.005f;

        public Vector3 position { get => transform.position; set => transform.position = value; }
        public Quaternion rotation { get => transform.rotation; set => transform.rotation = value; }

        public void MovePosition(Vector3 position) => transform.position = position;
        public void MoveRotation(Quaternion rot) => transform.rotation = rot;

        public unsafe void AddForce(Vector3 force, ForceMode mode = ForceMode.Force)
        {
            float* v = stackalloc float[3];
            v[0] = force.x; v[1] = force.y; v[2] = force.z;
            Native.Api.RigidbodyAddForce(m_GameObject.m_Id, v, (int)mode);
        }

        public void AddForce(float x, float y, float z, ForceMode mode = ForceMode.Force) => AddForce(new Vector3(x, y, z), mode);
        public void AddRelativeForce(Vector3 force, ForceMode mode = ForceMode.Force) => AddForce(transform.TransformDirection(force), mode);
        public void AddForceAtPosition(Vector3 force, Vector3 position, ForceMode mode = ForceMode.Force) => AddForce(force, mode);

        public void AddExplosionForce(float explosionForce, Vector3 explosionPosition, float explosionRadius,
                                      float upwardsModifier = 0f, ForceMode mode = ForceMode.Force)
        {
            Vector3 dir = transform.position - explosionPosition;
            float dist = dir.magnitude;
            if (explosionRadius > 0f && dist > explosionRadius)
                return;
            float falloff = explosionRadius > 0f ? 1f - dist / explosionRadius : 1f;
            dir = dist > 1e-4f ? dir / dist : Vector3.up;
            dir.y += upwardsModifier;
            AddForce(dir.normalized * explosionForce * falloff, mode);
        }

        public void Sleep() { }
        public void WakeUp() { }
        public bool IsSleeping() => false;
    }

    /// <summary>Collider base; the concrete shape classes below map onto the engine's single collider.</summary>
    public class Collider : NativeComponent
    {
        internal const int PropShape = 0, PropCenter = 1, PropSize = 2, PropRadius = 3, PropHeight = 4,
            PropIsTrigger = 5, PropFriction = 6, PropBounciness = 7;

        internal override BuiltinKind Kind => BuiltinKind.Collider;

        internal virtual int Shape => -1;

        internal override bool MatchesNative() => Shape < 0 || (int)GetFloat(PropShape) == Shape;

        public bool enabled { get; set; } = true;
        public bool isTrigger { get => GetBool(PropIsTrigger); set => SetBool(PropIsTrigger, value); }
        public Rigidbody attachedRigidbody => GetComponentInParent<Rigidbody>();

        public Bounds bounds
        {
            get
            {
                Vector3 c = transform.TransformPoint(GetVector3(PropCenter));
                Vector3 s = Vector3.Scale(transform.lossyScale, GetFloat(PropShape) switch
                {
                    0 => GetVector3(PropSize),
                    1 => Vector3.one * (GetFloat(PropRadius) * 2f),
                    _ => new Vector3(GetFloat(PropRadius) * 2f, GetFloat(PropHeight), GetFloat(PropRadius) * 2f),
                });
                return new Bounds(c, new Vector3(MathF.Abs(s.x), MathF.Abs(s.y), MathF.Abs(s.z)));
            }
        }

        public Vector3 ClosestPoint(Vector3 position) => bounds.ClosestPoint(position);
    }

    public sealed class BoxCollider : Collider
    {
        internal override int Shape => 0;
        public Vector3 center { get => GetVector3(PropCenter); set => SetVector3(PropCenter, value); }
        public Vector3 size { get => GetVector3(PropSize); set => SetVector3(PropSize, value); }
    }

    public sealed class SphereCollider : Collider
    {
        internal override int Shape => 1;
        public Vector3 center { get => GetVector3(PropCenter); set => SetVector3(PropCenter, value); }
        public float radius { get => GetFloat(PropRadius); set => SetFloat(PropRadius, value); }
    }

    public sealed class CapsuleCollider : Collider
    {
        internal override int Shape => 2;
        public Vector3 center { get => GetVector3(PropCenter); set => SetVector3(PropCenter, value); }
        public float radius { get => GetFloat(PropRadius); set => SetFloat(PropRadius, value); }
        public float height { get => GetFloat(PropHeight); set => SetFloat(PropHeight, value); }
        public int direction { get => 1; set { } }
    }

    public enum QueryTriggerInteraction { UseGlobal = 0, Ignore = 1, Collide = 2 }

    public struct RaycastHit
    {
        internal uint m_Entity;
        public Vector3 point { get; set; }
        public Vector3 normal { get; set; }
        public float distance { get; set; }
        public Collider collider => m_Entity != 0 ? World.GetGameObject(m_Entity)?.GetComponent<Collider>() : null;
        public Transform transform => m_Entity != 0 ? World.GetGameObject(m_Entity)?.transform : null;
        public Rigidbody rigidbody => collider != null ? collider.attachedRigidbody : null;
    }

    public static class Physics
    {
        public const int DefaultRaycastLayers = -5;
        public const int AllLayers = -1;
        public const int IgnoreRaycastLayer = 4;

        public static Vector3 gravity { get; set; } = new Vector3(0f, -9.81f, 0f);
        public static bool queriesHitTriggers { get; set; } = true;

        public static bool Raycast(Vector3 origin, Vector3 direction, float maxDistance = float.PositiveInfinity,
                                   int layerMask = DefaultRaycastLayers,
                                   QueryTriggerInteraction queryTriggerInteraction = QueryTriggerInteraction.UseGlobal) =>
            Raycast(origin, direction, out _, maxDistance, layerMask, queryTriggerInteraction);

        public static unsafe bool Raycast(Vector3 origin, Vector3 direction, out RaycastHit hitInfo,
                                          float maxDistance = float.PositiveInfinity, int layerMask = DefaultRaycastLayers,
                                          QueryTriggerInteraction queryTriggerInteraction = QueryTriggerInteraction.UseGlobal)
        {
            hitInfo = default;
            if (direction.sqrMagnitude < 1e-12f)
                return false;
            float* o = stackalloc float[3];
            float* d = stackalloc float[3];
            float* h = stackalloc float[7];
            o[0] = origin.x; o[1] = origin.y; o[2] = origin.z;
            d[0] = direction.x; d[1] = direction.y; d[2] = direction.z;
            uint entity = 0;
            // The engine caps "infinite" rays; 100 km is plenty for any scene.
            float distance = float.IsInfinity(maxDistance) ? 100000f : maxDistance;
            if (Native.Api.PhysicsRaycast(o, d, distance, h, &entity) == 0)
                return false;
            hitInfo.point = new Vector3(h[0], h[1], h[2]);
            hitInfo.normal = new Vector3(h[3], h[4], h[5]);
            hitInfo.distance = h[6];
            hitInfo.m_Entity = entity;
            return true;
        }

        public static bool Raycast(Ray ray, float maxDistance = float.PositiveInfinity, int layerMask = DefaultRaycastLayers,
                                   QueryTriggerInteraction queryTriggerInteraction = QueryTriggerInteraction.UseGlobal) =>
            Raycast(ray.origin, ray.direction, out _, maxDistance, layerMask, queryTriggerInteraction);

        public static bool Raycast(Ray ray, out RaycastHit hitInfo, float maxDistance = float.PositiveInfinity,
                                   int layerMask = DefaultRaycastLayers,
                                   QueryTriggerInteraction queryTriggerInteraction = QueryTriggerInteraction.UseGlobal) =>
            Raycast(ray.origin, ray.direction, out hitInfo, maxDistance, layerMask, queryTriggerInteraction);

        public static RaycastHit[] RaycastAll(Vector3 origin, Vector3 direction, float maxDistance = float.PositiveInfinity,
                                              int layerMask = DefaultRaycastLayers,
                                              QueryTriggerInteraction queryTriggerInteraction = QueryTriggerInteraction.UseGlobal) =>
            Raycast(origin, direction, out RaycastHit hit, maxDistance, layerMask, queryTriggerInteraction)
                ? new[] { hit }
                : Array.Empty<RaycastHit>();

        public static RaycastHit[] RaycastAll(Ray ray, float maxDistance = float.PositiveInfinity, int layerMask = DefaultRaycastLayers,
                                              QueryTriggerInteraction queryTriggerInteraction = QueryTriggerInteraction.UseGlobal) =>
            RaycastAll(ray.origin, ray.direction, maxDistance, layerMask, queryTriggerInteraction);

        public static int RaycastNonAlloc(Vector3 origin, Vector3 direction, RaycastHit[] results,
                                          float maxDistance = float.PositiveInfinity, int layerMask = DefaultRaycastLayers,
                                          QueryTriggerInteraction queryTriggerInteraction = QueryTriggerInteraction.UseGlobal)
        {
            if (results == null || results.Length == 0)
                return 0;
            if (!Raycast(origin, direction, out RaycastHit hit, maxDistance, layerMask, queryTriggerInteraction))
                return 0;
            results[0] = hit;
            return 1;
        }

        public static int RaycastNonAlloc(Ray ray, RaycastHit[] results, float maxDistance = float.PositiveInfinity,
                                          int layerMask = DefaultRaycastLayers,
                                          QueryTriggerInteraction queryTriggerInteraction = QueryTriggerInteraction.UseGlobal) =>
            RaycastNonAlloc(ray.origin, ray.direction, results, maxDistance, layerMask, queryTriggerInteraction);

        public static bool Linecast(Vector3 start, Vector3 end, int layerMask = DefaultRaycastLayers,
                                    QueryTriggerInteraction queryTriggerInteraction = QueryTriggerInteraction.UseGlobal) =>
            Linecast(start, end, out _, layerMask, queryTriggerInteraction);

        public static bool Linecast(Vector3 start, Vector3 end, out RaycastHit hitInfo, int layerMask = DefaultRaycastLayers,
                                    QueryTriggerInteraction queryTriggerInteraction = QueryTriggerInteraction.UseGlobal)
        {
            Vector3 d = end - start;
            return Raycast(start, d, out hitInfo, d.magnitude, layerMask, queryTriggerInteraction);
        }
    }

    [Serializable]
    public struct LayerMask
    {
        private int m_Mask;

        public int value { get => m_Mask; set => m_Mask = value; }

        public static implicit operator int(LayerMask mask) => mask.m_Mask;
        public static implicit operator LayerMask(int intVal) => new LayerMask { m_Mask = intVal };

        private static readonly string[] BuiltinLayers =
            { "Default", "TransparentFX", "Ignore Raycast", "", "Water", "UI" };

        public static string LayerToName(int layer) => layer >= 0 && layer < BuiltinLayers.Length ? BuiltinLayers[layer] : string.Empty;

        public static int NameToLayer(string layerName)
        {
            for (int i = 0; i < BuiltinLayers.Length; i++)
                if (BuiltinLayers[i] == layerName && layerName.Length > 0)
                    return i;
            return -1;
        }

        public static int GetMask(params string[] layerNames)
        {
            int mask = 0;
            foreach (string n in layerNames)
            {
                int layer = NameToLayer(n);
                if (layer >= 0)
                    mask |= 1 << layer;
            }
            return mask;
        }
    }

    // ------------------------------------------------------------------ rendering

    public class Shader : Object
    {
        public static Shader Find(string name) => new Shader { name = name };
        public static int PropertyToID(string name) => name?.GetHashCode() ?? 0;
        public static void SetGlobalFloat(string name, float value) { }
        public static void SetGlobalFloat(int nameID, float value) { }
        public static void SetGlobalColor(string name, Color value) { }
        public static void SetGlobalVector(string name, Vector4 value) { }
    }

    /// <summary>
    /// Material with the base color wired to the renderer it came from. Other properties are
    /// kept on the managed side until the material system lands.
    /// </summary>
    public class Material : Object
    {
        private readonly Renderer m_Owner;
        private Color m_Color = Color.white;
        private readonly Dictionary<string, object> m_Properties = new Dictionary<string, object>();

        public Material(Shader shader) { this.shader = shader; }
        public Material(Material source)
        {
            if (source is null)
                return;
            shader = source.shader;
            m_Color = source.color;
            name = source.name;
            foreach (var kv in source.m_Properties)
                m_Properties[kv.Key] = kv.Value;
        }

        internal Material(Renderer owner) { m_Owner = owner; name = "Default-Material"; }

        public Shader shader { get; set; }
        public int renderQueue { get; set; } = 2000;

        public Color color
        {
            get => m_Owner != null ? m_Owner.GetColor(MeshRenderer.PropColor) : m_Color;
            set
            {
                m_Color = value;
                if (m_Owner != null)
                    m_Owner.SetColor(MeshRenderer.PropColor, value);
            }
        }

        private static bool IsColorName(string n) => n == "_Color" || n == "_BaseColor";

        public bool HasProperty(string n) => IsColorName(n) || m_Properties.ContainsKey(n);
        public bool HasProperty(int nameID) => true;
        public void SetColor(string n, Color value) { if (IsColorName(n)) color = value; else m_Properties[n] = value; }
        public Color GetColor(string n) => IsColorName(n) ? color : m_Properties.TryGetValue(n, out object v) && v is Color c ? c : Color.white;
        public void SetFloat(string n, float value) => m_Properties[n] = value;
        public float GetFloat(string n) => m_Properties.TryGetValue(n, out object v) && v is float f ? f : 0f;
        public void SetFloat(int nameID, float value) { }
        public void SetColor(int nameID, Color value) { }
        public void SetInt(string n, int value) => m_Properties[n] = value;
        public void SetVector(string n, Vector4 value) => m_Properties[n] = value;
        public void EnableKeyword(string keyword) { }
        public void DisableKeyword(string keyword) { }
        public bool IsKeywordEnabled(string keyword) => false;
    }

    public class Renderer : NativeComponent
    {
        internal const int PropColor = 0;
        internal const int PropMesh = 1, PropTexture = 2;
        private Material m_Material;

        internal override BuiltinKind Kind => BuiltinKind.MeshRenderer;

        public bool enabled { get; set; } = true;
        public bool isVisible => enabled && gameObject.activeInHierarchy;

        public Material material
        {
            get => m_Material ??= new Material(this);
            set { if (value != null) SetColor(PropColor, value.color); }
        }

        public Material sharedMaterial { get => material; set => material = value; }
        public Material[] materials { get => new[] { material }; set { if (value != null && value.Length > 0) material = value[0]; } }
        public Material[] sharedMaterials { get => materials; set => materials = value; }

        public Bounds bounds => new Bounds(transform.position, transform.lossyScale);

        public void SetPropertyBlock(MaterialPropertyBlock properties)
        {
            if (properties != null && properties.TryGetColor(out Color c))
                SetColor(PropColor, c);
        }

        public void GetPropertyBlock(MaterialPropertyBlock dest) { }
    }

    public sealed class MeshRenderer : Renderer { }

    public sealed class MeshFilter : NativeComponent
    {
        internal override BuiltinKind Kind => BuiltinKind.MeshRenderer;
        public string meshName { get => GetString(Renderer.PropMesh); set => SetString(Renderer.PropMesh, value); }
    }

    public sealed class MaterialPropertyBlock
    {
        private Color? m_Color;
        private readonly Dictionary<string, object> m_Values = new Dictionary<string, object>();

        public bool isEmpty => m_Color == null && m_Values.Count == 0;
        public void Clear() { m_Color = null; m_Values.Clear(); }
        public void SetColor(string n, Color value) { if (n == "_Color" || n == "_BaseColor") m_Color = value; else m_Values[n] = value; }
        public void SetColor(int nameID, Color value) { }
        public void SetFloat(string n, float value) => m_Values[n] = value;
        public void SetFloat(int nameID, float value) { }
        public void SetVector(string n, Vector4 value) => m_Values[n] = value;
        public float GetFloat(string n) => m_Values.TryGetValue(n, out object v) && v is float f ? f : 0f;
        internal bool TryGetColor(out Color c) { c = m_Color ?? Color.white; return m_Color.HasValue; }
    }

    public enum LightType { Spot = 0, Directional = 1, Point = 2, Area = 3, Rectangle = 3, Disc = 4 }

    public enum LightShadows { None = 0, Hard = 1, Soft = 2 }

    public sealed class Light : NativeComponent
    {
        private const int PropColor = 0, PropIntensity = 1;
        internal override BuiltinKind Kind => BuiltinKind.Light;

        public bool enabled { get; set; } = true;
        public LightType type { get => LightType.Directional; set { } }
        public LightShadows shadows { get; set; } = LightShadows.Soft;
        public float range { get; set; } = 10f;
        public float spotAngle { get; set; } = 30f;
        public Color color { get => GetColor(PropColor); set => SetColor(PropColor, value); }
        public float intensity { get => GetFloat(PropIntensity); set => SetFloat(PropIntensity, value); }
    }

    public enum CameraClearFlags { Skybox = 1, SolidColor = 2, Depth = 3, Nothing = 4 }

    public sealed unsafe class Camera : NativeComponent
    {
        private const int PropFov = 0, PropNear = 1, PropFar = 2;
        internal override BuiltinKind Kind => BuiltinKind.Camera;

        public static Camera main
        {
            get
            {
                uint id = Native.Api.MainCamera();
                return id != 0 ? World.GetGameObject(id)?.GetComponent<Camera>() : null;
            }
        }

        public static Camera current => main;

        public bool enabled { get; set; } = true;
        public float fieldOfView { get => GetFloat(PropFov); set => SetFloat(PropFov, value); }
        public float nearClipPlane { get => GetFloat(PropNear); set => SetFloat(PropNear, value); }
        public float farClipPlane { get => GetFloat(PropFar); set => SetFloat(PropFar, value); }
        public bool orthographic { get => false; set { } }
        public float orthographicSize { get; set; } = 5f;
        public float depth { get; set; }
        public int cullingMask { get; set; } = -1;
        public CameraClearFlags clearFlags { get; set; } = CameraClearFlags.Skybox;
        public Color backgroundColor { get; set; } = new Color(0.19f, 0.3f, 0.47f, 0f);
        public int pixelWidth => Screen.width;
        public int pixelHeight => Screen.height;
        public float aspect => Screen.height > 0 ? (float)Screen.width / Screen.height : 1f;

        public Ray ScreenPointToRay(Vector3 position) =>
            ViewportPointToRay(new Vector3(position.x / Mathf.Max(1, Screen.width), position.y / Mathf.Max(1, Screen.height), 0f));

        public Ray ViewportPointToRay(Vector3 position)
        {
            float tanHalf = MathF.Tan(fieldOfView * 0.5f * Mathf.Deg2Rad);
            float x = (position.x * 2f - 1f) * tanHalf * aspect;
            float y = (position.y * 2f - 1f) * tanHalf;
            Transform t = transform;
            Vector3 origin = t.position;
            Vector3 dir = t.rotation * new Vector3(x, y, 1f);
            return new Ray(origin + dir.normalized * nearClipPlane, dir);
        }

        public Vector3 WorldToViewportPoint(Vector3 position)
        {
            Vector3 local = transform.InverseTransformDirection(position - transform.position);
            float tanHalf = MathF.Tan(fieldOfView * 0.5f * Mathf.Deg2Rad);
            if (MathF.Abs(local.z) < 1e-6f)
                return new Vector3(0.5f, 0.5f, local.z);
            float x = local.x / (local.z * tanHalf * aspect);
            float y = local.y / (local.z * tanHalf);
            return new Vector3(x * 0.5f + 0.5f, y * 0.5f + 0.5f, local.z);
        }

        public Vector3 WorldToScreenPoint(Vector3 position)
        {
            Vector3 v = WorldToViewportPoint(position);
            return new Vector3(v.x * Screen.width, v.y * Screen.height, v.z);
        }

        public Vector3 ViewportToWorldPoint(Vector3 position)
        {
            Ray ray = ViewportPointToRay(position);
            Vector3 dirLocal = transform.InverseTransformDirection(ray.direction);
            float scale = dirLocal.z > 1e-6f ? position.z / dirLocal.z : position.z;
            return transform.position + ray.direction * scale;
        }

        public Vector3 ScreenToWorldPoint(Vector3 position) =>
            ViewportToWorldPoint(new Vector3(position.x / Mathf.Max(1, Screen.width), position.y / Mathf.Max(1, Screen.height), position.z));

        public Vector3 ScreenToViewportPoint(Vector3 position) =>
            new Vector3(position.x / Mathf.Max(1, Screen.width), position.y / Mathf.Max(1, Screen.height), position.z);

        public Vector3 ViewportToScreenPoint(Vector3 position) =>
            new Vector3(position.x * Screen.width, position.y * Screen.height, position.z);
    }
}
