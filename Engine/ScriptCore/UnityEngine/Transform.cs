using System;
using System.Collections;
using IndeetsEngine.Interop;
using IndeetsEngine.Runtime;

namespace UnityEngine
{
    public enum Space { World = 0, Self = 1 }

    /// <summary>
    /// Position, rotation and scale of a GameObject. Values live in the engine; every property
    /// reads and writes through, so physics, the editor and scripts always agree.
    /// </summary>
    public unsafe class Transform : Component, IEnumerable
    {
        internal Transform(GameObject owner)
        {
            m_GameObject = owner;
        }

        private uint Id => m_GameObject.m_Id;

        // ---- Raw access

        private unsafe void GetLocal(out Vector3 position, out Quaternion rotation, out Vector3 scale)
        {
            float* p = stackalloc float[3];
            float* r = stackalloc float[4];
            float* s = stackalloc float[3];
            Native.Api.TransformGetLocal(Id, p, r, s);
            position = new Vector3(p[0], p[1], p[2]);
            rotation = new Quaternion(r[0], r[1], r[2], r[3]);
            scale = new Vector3(s[0], s[1], s[2]);
        }

        private unsafe void SetLocal(Vector3? position, Quaternion? rotation, Vector3? scale)
        {
            float* p = stackalloc float[3];
            float* r = stackalloc float[4];
            float* s = stackalloc float[3];
            if (position is Vector3 pv) { p[0] = pv.x; p[1] = pv.y; p[2] = pv.z; }
            if (rotation is Quaternion rv) { rv = rv.normalized; r[0] = rv.x; r[1] = rv.y; r[2] = rv.z; r[3] = rv.w; }
            if (scale is Vector3 sv) { s[0] = sv.x; s[1] = sv.y; s[2] = sv.z; }
            Native.Api.TransformSetLocal(Id, position.HasValue ? p : null, rotation.HasValue ? r : null, scale.HasValue ? s : null);
        }

        private unsafe void GetWorld(out Vector3 position, out Quaternion rotation, out Vector3 scale)
        {
            float* p = stackalloc float[3];
            float* r = stackalloc float[4];
            float* s = stackalloc float[3];
            Native.Api.TransformGetWorld(Id, p, r, s);
            position = new Vector3(p[0], p[1], p[2]);
            rotation = new Quaternion(r[0], r[1], r[2], r[3]);
            scale = new Vector3(s[0], s[1], s[2]);
        }

        private unsafe void SetWorld(Vector3? position, Quaternion? rotation)
        {
            float* p = stackalloc float[3];
            float* r = stackalloc float[4];
            if (position is Vector3 pv) { p[0] = pv.x; p[1] = pv.y; p[2] = pv.z; }
            if (rotation is Quaternion rv) { rv = rv.normalized; r[0] = rv.x; r[1] = rv.y; r[2] = rv.z; r[3] = rv.w; }
            Native.Api.TransformSetWorld(Id, position.HasValue ? p : null, rotation.HasValue ? r : null);
        }

        // ---- Properties

        public Vector3 position
        {
            get { GetWorld(out Vector3 p, out _, out _); return p; }
            set => SetWorld(value, null);
        }

        public Quaternion rotation
        {
            get { GetWorld(out _, out Quaternion r, out _); return r; }
            set => SetWorld(null, value);
        }

        public Vector3 lossyScale
        {
            get { GetWorld(out _, out _, out Vector3 s); return s; }
        }

        public Vector3 localPosition
        {
            get { GetLocal(out Vector3 p, out _, out _); return p; }
            set => SetLocal(value, null, null);
        }

        public Quaternion localRotation
        {
            get { GetLocal(out _, out Quaternion r, out _); return r; }
            set => SetLocal(null, value, null);
        }

        public Vector3 localScale
        {
            get { GetLocal(out _, out _, out Vector3 s); return s; }
            set => SetLocal(null, null, value);
        }

        public Vector3 eulerAngles
        {
            get => rotation.eulerAngles;
            set => rotation = Quaternion.Euler(value);
        }

        public Vector3 localEulerAngles
        {
            get => localRotation.eulerAngles;
            set => localRotation = Quaternion.Euler(value);
        }

        public Vector3 forward
        {
            get => rotation * Vector3.forward;
            set => rotation = Quaternion.LookRotation(value);
        }

        public Vector3 right
        {
            get => rotation * Vector3.right;
            set => rotation = Quaternion.FromToRotation(Vector3.right, value);
        }

        public Vector3 up
        {
            get => rotation * Vector3.up;
            set => rotation = Quaternion.FromToRotation(Vector3.up, value);
        }

        public bool hasChanged { get; set; } = true;

        public void SetPositionAndRotation(Vector3 position, Quaternion rotation) => SetWorld(position, rotation);

        public void SetLocalPositionAndRotation(Vector3 localPosition, Quaternion localRotation) =>
            SetLocal(localPosition, localRotation, null);

        public void GetPositionAndRotation(out Vector3 position, out Quaternion rotation) => GetWorld(out position, out rotation, out _);

        public void GetLocalPositionAndRotation(out Vector3 localPosition, out Quaternion localRotation) =>
            GetLocal(out localPosition, out localRotation, out _);

        // ---- Hierarchy

        public Transform parent
        {
            get
            {
                uint p = Native.Api.EntityGetParent(Id);
                return p != 0 ? World.GetGameObject(p)?.transform : null;
            }
            set => SetParent(value, true);
        }

        public Transform root
        {
            get
            {
                Transform t = this;
                for (Transform p = t.parent; p != null; p = p.parent)
                    t = p;
                return t;
            }
        }

        public void SetParent(Transform p) => SetParent(p, true);

        public void SetParent(Transform p, bool worldPositionStays)
        {
            uint parentId = p != null ? p.m_GameObject.m_Id : 0u;
            Native.Api.EntitySetParent(Id, parentId, worldPositionStays ? 1 : 0);
            World.SyncActivation();
        }

        public unsafe int childCount
        {
            get => Native.Api.EntityGetChildren(Id, null, 0);
        }

        public unsafe Transform GetChild(int index)
        {
            int count = childCount;
            if (index < 0 || index >= count)
                throw new UnityException("Transform child out of bounds");
            uint* ids = stackalloc uint[count];
            Native.Api.EntityGetChildren(Id, ids, count);
            return World.GetGameObject(ids[index]).transform;
        }

        public Transform Find(string n)
        {
            if (string.IsNullOrEmpty(n))
                return null;
            Transform current = this;
            foreach (string part in n.Split('/'))
            {
                Transform next = null;
                for (int i = 0; i < current.childCount; i++)
                {
                    Transform child = current.GetChild(i);
                    if (child.name == part)
                    {
                        next = child;
                        break;
                    }
                }
                if (next == null)
                    return null;
                current = next;
            }
            return current;
        }

        public Transform FindChild(string n) => Find(n);

        public bool IsChildOf(Transform p)
        {
            for (Transform t = this; t != null; t = t.parent)
                if (t == p)
                    return true;
            return false;
        }

        public void DetachChildren()
        {
            for (int i = childCount - 1; i >= 0; i--)
                GetChild(i).SetParent(null, true);
        }

        public int GetSiblingIndex()
        {
            Transform p = parent;
            if (p == null)
                return 0;
            for (int i = 0; i < p.childCount; i++)
                if (p.GetChild(i) == this)
                    return i;
            return 0;
        }

        public IEnumerator GetEnumerator()
        {
            int count = childCount;
            var children = new Transform[count];
            for (int i = 0; i < count; i++)
                children[i] = GetChild(i);
            return children.GetEnumerator();
        }

        // ---- Movement

        public void Translate(Vector3 translation) => Translate(translation, Space.Self);

        public void Translate(Vector3 translation, Space relativeTo)
        {
            if (relativeTo == Space.World)
                position += translation;
            else
                position += TransformDirection(translation);
        }

        public void Translate(float x, float y, float z) => Translate(new Vector3(x, y, z), Space.Self);
        public void Translate(float x, float y, float z, Space relativeTo) => Translate(new Vector3(x, y, z), relativeTo);

        public void Translate(Vector3 translation, Transform relativeTo)
        {
            position += relativeTo != null ? relativeTo.TransformDirection(translation) : translation;
        }

        public void Rotate(Vector3 eulers) => Rotate(eulers, Space.Self);

        public void Rotate(Vector3 eulers, Space relativeTo)
        {
            Quaternion q = Quaternion.Euler(eulers);
            if (relativeTo == Space.Self)
                localRotation = localRotation * q;
            else
                rotation = q * rotation;
        }

        public void Rotate(float xAngle, float yAngle, float zAngle) => Rotate(new Vector3(xAngle, yAngle, zAngle), Space.Self);
        public void Rotate(float xAngle, float yAngle, float zAngle, Space relativeTo) =>
            Rotate(new Vector3(xAngle, yAngle, zAngle), relativeTo);

        public void Rotate(Vector3 axis, float angle) => Rotate(axis, angle, Space.Self);

        public void Rotate(Vector3 axis, float angle, Space relativeTo)
        {
            if (relativeTo == Space.Self)
                rotation = rotation * Quaternion.AngleAxis(angle, axis);
            else
                rotation = Quaternion.AngleAxis(angle, axis) * rotation;
        }

        public void RotateAround(Vector3 point, Vector3 axis, float angle)
        {
            Quaternion q = Quaternion.AngleAxis(angle, axis);
            GetWorld(out Vector3 p, out Quaternion r, out _);
            SetWorld(point + q * (p - point), q * r);
        }

        public void LookAt(Transform target) => LookAt(target.position, Vector3.up);
        public void LookAt(Transform target, Vector3 worldUp) => LookAt(target.position, worldUp);
        public void LookAt(Vector3 worldPosition) => LookAt(worldPosition, Vector3.up);

        public void LookAt(Vector3 worldPosition, Vector3 worldUp)
        {
            Vector3 dir = worldPosition - position;
            if (dir.sqrMagnitude > 1e-12f)
                rotation = Quaternion.LookRotation(dir, worldUp);
        }

        // ---- Space conversion

        public Vector3 TransformPoint(Vector3 p)
        {
            GetWorld(out Vector3 wp, out Quaternion wr, out Vector3 ws);
            return wp + wr * Vector3.Scale(p, ws);
        }

        public Vector3 TransformPoint(float x, float y, float z) => TransformPoint(new Vector3(x, y, z));

        public Vector3 InverseTransformPoint(Vector3 p)
        {
            GetWorld(out Vector3 wp, out Quaternion wr, out Vector3 ws);
            Vector3 local = Quaternion.Inverse(wr) * (p - wp);
            return new Vector3(SafeDiv(local.x, ws.x), SafeDiv(local.y, ws.y), SafeDiv(local.z, ws.z));
        }

        public Vector3 InverseTransformPoint(float x, float y, float z) => InverseTransformPoint(new Vector3(x, y, z));

        public Vector3 TransformDirection(Vector3 direction) => rotation * direction;
        public Vector3 TransformDirection(float x, float y, float z) => TransformDirection(new Vector3(x, y, z));
        public Vector3 InverseTransformDirection(Vector3 direction) => Quaternion.Inverse(rotation) * direction;
        public Vector3 InverseTransformDirection(float x, float y, float z) => InverseTransformDirection(new Vector3(x, y, z));

        public Vector3 TransformVector(Vector3 vector)
        {
            GetWorld(out _, out Quaternion wr, out Vector3 ws);
            return wr * Vector3.Scale(vector, ws);
        }

        public Vector3 InverseTransformVector(Vector3 vector)
        {
            GetWorld(out _, out Quaternion wr, out Vector3 ws);
            Vector3 local = Quaternion.Inverse(wr) * vector;
            return new Vector3(SafeDiv(local.x, ws.x), SafeDiv(local.y, ws.y), SafeDiv(local.z, ws.z));
        }

        private static float SafeDiv(float a, float b) => MathF.Abs(b) > 1e-12f ? a / b : 0f;
    }

    public class UnityException : Exception
    {
        public UnityException() { }
        public UnityException(string message) : base(message) { }
        public UnityException(string message, Exception inner) : base(message, inner) { }
    }

    public class MissingReferenceException : Exception
    {
        public MissingReferenceException() { }
        public MissingReferenceException(string message) : base(message) { }
    }

    public class MissingComponentException : Exception
    {
        public MissingComponentException() { }
        public MissingComponentException(string message) : base(message) { }
    }
}
