using System;
using System.Globalization;

namespace UnityEngine
{
    /// <summary>
    /// Rotation, same conventions as Unity: left-handed, +Y up, +Z forward, Euler angles
    /// applied Z, then X, then Y (q = qY * qX * qZ).
    /// </summary>
    [Serializable]
    public struct Quaternion : IEquatable<Quaternion>
    {
        public const float kEpsilon = 1e-6f;
        public float x;
        public float y;
        public float z;
        public float w;

        public Quaternion(float x, float y, float z, float w) { this.x = x; this.y = y; this.z = z; this.w = w; }

        public static Quaternion identity => new Quaternion(0f, 0f, 0f, 1f);

        public float this[int index]
        {
            get => index switch { 0 => x, 1 => y, 2 => z, 3 => w, _ => throw new IndexOutOfRangeException("Invalid Quaternion index!") };
            set
            {
                switch (index)
                {
                    case 0: x = value; break;
                    case 1: y = value; break;
                    case 2: z = value; break;
                    case 3: w = value; break;
                    default: throw new IndexOutOfRangeException("Invalid Quaternion index!");
                }
            }
        }

        public Quaternion normalized => Normalize(this);
        public void Normalize() => this = Normalize(this);
        public void Set(float newX, float newY, float newZ, float newW) { x = newX; y = newY; z = newZ; w = newW; }

        public Vector3 eulerAngles
        {
            get => ToEuler(this);
            set => this = Euler(value);
        }

        public static Quaternion Normalize(Quaternion q)
        {
            float mag = MathF.Sqrt(Dot(q, q));
            return mag < Mathf.Epsilon ? identity : new Quaternion(q.x / mag, q.y / mag, q.z / mag, q.w / mag);
        }

        public static float Dot(Quaternion a, Quaternion b) => a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;

        public static Quaternion AngleAxis(float angle, Vector3 axis)
        {
            if (axis.sqrMagnitude < 1e-12f)
                return identity;
            axis = axis.normalized;
            float half = angle * Mathf.Deg2Rad * 0.5f;
            float s = MathF.Sin(half);
            return new Quaternion(axis.x * s, axis.y * s, axis.z * s, MathF.Cos(half));
        }

        public void ToAngleAxis(out float angle, out Vector3 axis)
        {
            Quaternion q = w < 0f ? new Quaternion(-x, -y, -z, -w) : this;
            q = Normalize(q);
            angle = 2f * MathF.Acos(Mathf.Clamp(q.w, -1f, 1f)) * Mathf.Rad2Deg;
            float s = MathF.Sqrt(MathF.Max(0f, 1f - q.w * q.w));
            axis = s < 1e-4f ? Vector3.right : new Vector3(q.x / s, q.y / s, q.z / s);
        }

        public static Quaternion Euler(float x, float y, float z) => Euler(new Vector3(x, y, z));

        public static Quaternion Euler(Vector3 euler) =>
            AngleAxis(euler.y, Vector3.up) * AngleAxis(euler.x, Vector3.right) * AngleAxis(euler.z, Vector3.forward);

        private static Vector3 ToEuler(Quaternion q)
        {
            q = Normalize(q);
            // Rotation matrix elements (row, column) of R = Ry * Rx * Rz.
            float r02 = 2f * (q.x * q.z + q.w * q.y);
            float r22 = 1f - 2f * (q.x * q.x + q.y * q.y);
            float r12 = 2f * (q.y * q.z - q.w * q.x);
            float r10 = 2f * (q.x * q.y + q.w * q.z);
            float r11 = 1f - 2f * (q.x * q.x + q.z * q.z);
            float r00 = 1f - 2f * (q.y * q.y + q.z * q.z);
            float r20 = 2f * (q.x * q.z - q.w * q.y);

            float ex = MathF.Asin(Mathf.Clamp(-r12, -1f, 1f));
            float ey, ez;
            if (MathF.Cos(ex) > 1e-4f)
            {
                ey = MathF.Atan2(r02, r22);
                ez = MathF.Atan2(r10, r11);
            }
            else
            {
                ey = MathF.Atan2(-r20, r00);
                ez = 0f;
            }

            return new Vector3(Wrap360(ex * Mathf.Rad2Deg), Wrap360(ey * Mathf.Rad2Deg), Wrap360(ez * Mathf.Rad2Deg));
        }

        private static float Wrap360(float degrees)
        {
            degrees %= 360f;
            if (degrees < 0f) degrees += 360f;
            return degrees >= 359.9999f ? 0f : degrees;
        }

        public static Quaternion LookRotation(Vector3 forward) => LookRotation(forward, Vector3.up);

        public static Quaternion LookRotation(Vector3 forward, Vector3 upwards)
        {
            if (forward.sqrMagnitude < 1e-12f)
                return identity;
            Vector3 f = forward.normalized;
            Vector3 r = Vector3.Cross(upwards, f);
            if (r.sqrMagnitude < 1e-12f)
                r = Vector3.Cross(MathF.Abs(f.y) < 0.99f ? Vector3.up : Vector3.forward, f);
            r = r.normalized;
            Vector3 u = Vector3.Cross(f, r);
            return FromBasis(r, u, f);
        }

        /// <summary>Rotation whose columns are the given orthonormal right / up / forward axes.</summary>
        internal static Quaternion FromBasis(Vector3 r, Vector3 u, Vector3 f)
        {
            float m00 = r.x, m01 = u.x, m02 = f.x;
            float m10 = r.y, m11 = u.y, m12 = f.y;
            float m20 = r.z, m21 = u.z, m22 = f.z;
            float trace = m00 + m11 + m22;
            Quaternion q;
            if (trace > 0f)
            {
                float s = MathF.Sqrt(trace + 1f) * 2f;
                q = new Quaternion((m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s);
            }
            else if (m00 > m11 && m00 > m22)
            {
                float s = MathF.Sqrt(1f + m00 - m11 - m22) * 2f;
                q = new Quaternion(0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s);
            }
            else if (m11 > m22)
            {
                float s = MathF.Sqrt(1f + m11 - m00 - m22) * 2f;
                q = new Quaternion((m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s);
            }
            else
            {
                float s = MathF.Sqrt(1f + m22 - m00 - m11) * 2f;
                q = new Quaternion((m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s);
            }
            return Normalize(q);
        }

        public void SetLookRotation(Vector3 view) => this = LookRotation(view);
        public void SetLookRotation(Vector3 view, Vector3 up) => this = LookRotation(view, up);

        public static Quaternion FromToRotation(Vector3 fromDirection, Vector3 toDirection)
        {
            Vector3 a = fromDirection.normalized, b = toDirection.normalized;
            float d = Vector3.Dot(a, b);
            if (d >= 1f - 1e-6f)
                return identity;
            if (d <= -1f + 1e-6f)
            {
                Vector3 axis = Vector3.Cross(Vector3.right, a);
                if (axis.sqrMagnitude < 1e-6f)
                    axis = Vector3.Cross(Vector3.up, a);
                return AngleAxis(180f, axis);
            }
            Vector3 c = Vector3.Cross(a, b);
            return Normalize(new Quaternion(c.x, c.y, c.z, 1f + d));
        }

        public void SetFromToRotation(Vector3 fromDirection, Vector3 toDirection) =>
            this = FromToRotation(fromDirection, toDirection);

        public static Quaternion Inverse(Quaternion q)
        {
            float n = Dot(q, q);
            return n < Mathf.Epsilon ? identity : new Quaternion(-q.x / n, -q.y / n, -q.z / n, q.w / n);
        }

        public static float Angle(Quaternion a, Quaternion b)
        {
            float d = MathF.Min(MathF.Abs(Dot(Normalize(a), Normalize(b))), 1f);
            return d > 1f - kEpsilon ? 0f : MathF.Acos(d) * 2f * Mathf.Rad2Deg;
        }

        public static Quaternion Lerp(Quaternion a, Quaternion b, float t) => LerpUnclamped(a, b, Mathf.Clamp01(t));

        public static Quaternion LerpUnclamped(Quaternion a, Quaternion b, float t)
        {
            if (Dot(a, b) < 0f)
                b = new Quaternion(-b.x, -b.y, -b.z, -b.w);
            return Normalize(new Quaternion(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                                            a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t));
        }

        public static Quaternion Slerp(Quaternion a, Quaternion b, float t) => SlerpUnclamped(a, b, Mathf.Clamp01(t));

        public static Quaternion SlerpUnclamped(Quaternion a, Quaternion b, float t)
        {
            a = Normalize(a);
            b = Normalize(b);
            float cos = Dot(a, b);
            if (cos < 0f)
            {
                b = new Quaternion(-b.x, -b.y, -b.z, -b.w);
                cos = -cos;
            }
            if (cos > 0.9995f)
                return LerpUnclamped(a, b, t);
            float theta = MathF.Acos(cos);
            float sin = MathF.Sin(theta);
            float wa = MathF.Sin((1f - t) * theta) / sin;
            float wb = MathF.Sin(t * theta) / sin;
            return new Quaternion(a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb);
        }

        public static Quaternion RotateTowards(Quaternion from, Quaternion to, float maxDegreesDelta)
        {
            float angle = Angle(from, to);
            if (angle == 0f)
                return to;
            return SlerpUnclamped(from, to, MathF.Min(1f, maxDegreesDelta / angle));
        }

        public static Quaternion operator *(Quaternion a, Quaternion b) => new Quaternion(
            a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y + a.y * b.w + a.z * b.x - a.x * b.z,
            a.w * b.z + a.z * b.w + a.x * b.y - a.y * b.x,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z);

        public static Vector3 operator *(Quaternion q, Vector3 v)
        {
            // v' = v + 2 * cross(q.xyz, cross(q.xyz, v) + q.w * v)
            float tx = 2f * (q.y * v.z - q.z * v.y);
            float ty = 2f * (q.z * v.x - q.x * v.z);
            float tz = 2f * (q.x * v.y - q.y * v.x);
            return new Vector3(
                v.x + q.w * tx + (q.y * tz - q.z * ty),
                v.y + q.w * ty + (q.z * tx - q.x * tz),
                v.z + q.w * tz + (q.x * ty - q.y * tx));
        }

        public static bool operator ==(Quaternion a, Quaternion b) => Dot(a, b) > 1f - kEpsilon;
        public static bool operator !=(Quaternion a, Quaternion b) => !(a == b);

        public bool Equals(Quaternion other) => x.Equals(other.x) && y.Equals(other.y) && z.Equals(other.z) && w.Equals(other.w);
        public override bool Equals(object other) => other is Quaternion q && Equals(q);
        public override int GetHashCode() => HashCode.Combine(x, y, z, w);
        public override string ToString() =>
            string.Format(CultureInfo.InvariantCulture, "({0:F5}, {1:F5}, {2:F5}, {3:F5})", x, y, z, w);
    }
}
