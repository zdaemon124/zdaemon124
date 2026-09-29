using System;
using System.Globalization;

namespace UnityEngine
{
    [Serializable]
    public struct Vector2 : IEquatable<Vector2>, IFormattable
    {
        public float x;
        public float y;

        public Vector2(float x, float y) { this.x = x; this.y = y; }

        public float this[int index]
        {
            get => index switch { 0 => x, 1 => y, _ => throw new IndexOutOfRangeException("Invalid Vector2 index!") };
            set { if (index == 0) x = value; else if (index == 1) y = value; else throw new IndexOutOfRangeException("Invalid Vector2 index!"); }
        }

        public static Vector2 zero => new Vector2(0f, 0f);
        public static Vector2 one => new Vector2(1f, 1f);
        public static Vector2 up => new Vector2(0f, 1f);
        public static Vector2 down => new Vector2(0f, -1f);
        public static Vector2 left => new Vector2(-1f, 0f);
        public static Vector2 right => new Vector2(1f, 0f);
        public static Vector2 positiveInfinity => new Vector2(float.PositiveInfinity, float.PositiveInfinity);
        public static Vector2 negativeInfinity => new Vector2(float.NegativeInfinity, float.NegativeInfinity);

        public float sqrMagnitude => x * x + y * y;
        public float magnitude => MathF.Sqrt(x * x + y * y);

        public Vector2 normalized
        {
            get
            {
                float m = magnitude;
                return m > 1e-5f ? this / m : zero;
            }
        }

        public void Normalize() => this = normalized;
        public void Set(float newX, float newY) { x = newX; y = newY; }
        public void Scale(Vector2 scale) { x *= scale.x; y *= scale.y; }

        public static float Dot(Vector2 a, Vector2 b) => a.x * b.x + a.y * b.y;
        public static float Distance(Vector2 a, Vector2 b) => (a - b).magnitude;
        public static Vector2 Lerp(Vector2 a, Vector2 b, float t) { t = Mathf.Clamp01(t); return a + (b - a) * t; }
        public static Vector2 LerpUnclamped(Vector2 a, Vector2 b, float t) => a + (b - a) * t;
        public static Vector2 Min(Vector2 a, Vector2 b) => new Vector2(MathF.Min(a.x, b.x), MathF.Min(a.y, b.y));
        public static Vector2 Max(Vector2 a, Vector2 b) => new Vector2(MathF.Max(a.x, b.x), MathF.Max(a.y, b.y));
        public static Vector2 Scale(Vector2 a, Vector2 b) => new Vector2(a.x * b.x, a.y * b.y);
        public static Vector2 Perpendicular(Vector2 d) => new Vector2(-d.y, d.x);
        public static Vector2 Reflect(Vector2 d, Vector2 n) => d - 2f * Dot(d, n) * n;

        public static Vector2 ClampMagnitude(Vector2 v, float maxLength)
        {
            float sq = v.sqrMagnitude;
            return sq > maxLength * maxLength ? v / MathF.Sqrt(sq) * maxLength : v;
        }

        public static Vector2 MoveTowards(Vector2 current, Vector2 target, float maxDelta)
        {
            Vector2 d = target - current;
            float dist = d.magnitude;
            return dist <= maxDelta || dist == 0f ? target : current + d / dist * maxDelta;
        }

        public static float Angle(Vector2 from, Vector2 to)
        {
            float denom = MathF.Sqrt(from.sqrMagnitude * to.sqrMagnitude);
            if (denom < 1e-15f) return 0f;
            return MathF.Acos(Mathf.Clamp(Dot(from, to) / denom, -1f, 1f)) * Mathf.Rad2Deg;
        }

        public static float SignedAngle(Vector2 from, Vector2 to) =>
            Angle(from, to) * MathF.Sign(from.x * to.y - from.y * to.x);

        public static Vector2 SmoothDamp(Vector2 current, Vector2 target, ref Vector2 velocity, float smoothTime,
                                         float maxSpeed = float.PositiveInfinity, float deltaTime = -1f)
        {
            if (deltaTime < 0f) deltaTime = Time.deltaTime;
            float vx = velocity.x, vy = velocity.y;
            float rx = Mathf.SmoothDamp(current.x, target.x, ref vx, smoothTime, maxSpeed, deltaTime);
            float ry = Mathf.SmoothDamp(current.y, target.y, ref vy, smoothTime, maxSpeed, deltaTime);
            velocity = new Vector2(vx, vy);
            return new Vector2(rx, ry);
        }

        public static Vector2 operator +(Vector2 a, Vector2 b) => new Vector2(a.x + b.x, a.y + b.y);
        public static Vector2 operator -(Vector2 a, Vector2 b) => new Vector2(a.x - b.x, a.y - b.y);
        public static Vector2 operator *(Vector2 a, Vector2 b) => new Vector2(a.x * b.x, a.y * b.y);
        public static Vector2 operator /(Vector2 a, Vector2 b) => new Vector2(a.x / b.x, a.y / b.y);
        public static Vector2 operator -(Vector2 a) => new Vector2(-a.x, -a.y);
        public static Vector2 operator *(Vector2 a, float d) => new Vector2(a.x * d, a.y * d);
        public static Vector2 operator *(float d, Vector2 a) => new Vector2(a.x * d, a.y * d);
        public static Vector2 operator /(Vector2 a, float d) => new Vector2(a.x / d, a.y / d);
        public static bool operator ==(Vector2 a, Vector2 b) => (a - b).sqrMagnitude < 9.99999944E-11f;
        public static bool operator !=(Vector2 a, Vector2 b) => !(a == b);
        public static implicit operator Vector2(Vector3 v) => new Vector2(v.x, v.y);
        public static implicit operator Vector3(Vector2 v) => new Vector3(v.x, v.y, 0f);

        public bool Equals(Vector2 other) => x.Equals(other.x) && y.Equals(other.y);
        public override bool Equals(object other) => other is Vector2 v && Equals(v);
        public override int GetHashCode() => HashCode.Combine(x, y);
        public override string ToString() => ToString(null, null);
        public string ToString(string format) => ToString(format, null);
        public string ToString(string format, IFormatProvider provider)
        {
            format ??= "F2";
            provider ??= CultureInfo.InvariantCulture;
            return $"({x.ToString(format, provider)}, {y.ToString(format, provider)})";
        }
    }

    [Serializable]
    public struct Vector3 : IEquatable<Vector3>, IFormattable
    {
        public const float kEpsilon = 1e-5f;
        public float x;
        public float y;
        public float z;

        public Vector3(float x, float y, float z) { this.x = x; this.y = y; this.z = z; }
        public Vector3(float x, float y) { this.x = x; this.y = y; z = 0f; }

        public float this[int index]
        {
            get => index switch { 0 => x, 1 => y, 2 => z, _ => throw new IndexOutOfRangeException("Invalid Vector3 index!") };
            set
            {
                switch (index)
                {
                    case 0: x = value; break;
                    case 1: y = value; break;
                    case 2: z = value; break;
                    default: throw new IndexOutOfRangeException("Invalid Vector3 index!");
                }
            }
        }

        public static Vector3 zero => new Vector3(0f, 0f, 0f);
        public static Vector3 one => new Vector3(1f, 1f, 1f);
        public static Vector3 up => new Vector3(0f, 1f, 0f);
        public static Vector3 down => new Vector3(0f, -1f, 0f);
        public static Vector3 left => new Vector3(-1f, 0f, 0f);
        public static Vector3 right => new Vector3(1f, 0f, 0f);
        public static Vector3 forward => new Vector3(0f, 0f, 1f);
        public static Vector3 back => new Vector3(0f, 0f, -1f);
        public static Vector3 positiveInfinity => new Vector3(float.PositiveInfinity, float.PositiveInfinity, float.PositiveInfinity);
        public static Vector3 negativeInfinity => new Vector3(float.NegativeInfinity, float.NegativeInfinity, float.NegativeInfinity);

        public float sqrMagnitude => x * x + y * y + z * z;
        public float magnitude => MathF.Sqrt(x * x + y * y + z * z);
        public Vector3 normalized => Normalize(this);

        public void Normalize() => this = Normalize(this);
        public void Set(float newX, float newY, float newZ) { x = newX; y = newY; z = newZ; }
        public void Scale(Vector3 scale) { x *= scale.x; y *= scale.y; z *= scale.z; }

        public static Vector3 Normalize(Vector3 v)
        {
            float m = v.magnitude;
            return m > kEpsilon ? v / m : zero;
        }

        public static float Dot(Vector3 a, Vector3 b) => a.x * b.x + a.y * b.y + a.z * b.z;
        public static Vector3 Cross(Vector3 a, Vector3 b) =>
            new Vector3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
        public static float Distance(Vector3 a, Vector3 b) => (a - b).magnitude;
        public static float Magnitude(Vector3 v) => v.magnitude;
        public static float SqrMagnitude(Vector3 v) => v.sqrMagnitude;
        public static Vector3 Scale(Vector3 a, Vector3 b) => new Vector3(a.x * b.x, a.y * b.y, a.z * b.z);
        public static Vector3 Min(Vector3 a, Vector3 b) => new Vector3(MathF.Min(a.x, b.x), MathF.Min(a.y, b.y), MathF.Min(a.z, b.z));
        public static Vector3 Max(Vector3 a, Vector3 b) => new Vector3(MathF.Max(a.x, b.x), MathF.Max(a.y, b.y), MathF.Max(a.z, b.z));
        public static Vector3 Lerp(Vector3 a, Vector3 b, float t) { t = Mathf.Clamp01(t); return a + (b - a) * t; }
        public static Vector3 LerpUnclamped(Vector3 a, Vector3 b, float t) => a + (b - a) * t;
        public static Vector3 Reflect(Vector3 direction, Vector3 normal) => direction - 2f * Dot(normal, direction) * normal;

        public static Vector3 Project(Vector3 v, Vector3 onNormal)
        {
            float sq = onNormal.sqrMagnitude;
            return sq < Mathf.Epsilon ? zero : onNormal * (Dot(v, onNormal) / sq);
        }

        public static Vector3 ProjectOnPlane(Vector3 v, Vector3 planeNormal)
        {
            float sq = planeNormal.sqrMagnitude;
            return sq < Mathf.Epsilon ? v : v - planeNormal * (Dot(v, planeNormal) / sq);
        }

        public static Vector3 ClampMagnitude(Vector3 v, float maxLength)
        {
            float sq = v.sqrMagnitude;
            return sq > maxLength * maxLength ? v / MathF.Sqrt(sq) * maxLength : v;
        }

        public static Vector3 MoveTowards(Vector3 current, Vector3 target, float maxDistanceDelta)
        {
            Vector3 d = target - current;
            float dist = d.magnitude;
            return dist <= maxDistanceDelta || dist == 0f ? target : current + d / dist * maxDistanceDelta;
        }

        public static float Angle(Vector3 from, Vector3 to)
        {
            float denom = MathF.Sqrt(from.sqrMagnitude * to.sqrMagnitude);
            if (denom < 1e-15f) return 0f;
            return MathF.Acos(Mathf.Clamp(Dot(from, to) / denom, -1f, 1f)) * Mathf.Rad2Deg;
        }

        public static float SignedAngle(Vector3 from, Vector3 to, Vector3 axis)
        {
            float angle = Angle(from, to);
            Vector3 c = Cross(from, to);
            return angle * MathF.Sign(axis.x * c.x + axis.y * c.y + axis.z * c.z);
        }

        public static Vector3 Slerp(Vector3 a, Vector3 b, float t) => SlerpUnclamped(a, b, Mathf.Clamp01(t));

        public static Vector3 SlerpUnclamped(Vector3 a, Vector3 b, float t)
        {
            float magA = a.magnitude, magB = b.magnitude;
            if (magA < kEpsilon || magB < kEpsilon)
                return LerpUnclamped(a, b, t);
            Vector3 na = a / magA, nb = b / magB;
            float dot = Mathf.Clamp(Dot(na, nb), -1f, 1f);
            float theta = MathF.Acos(dot) * t;
            Vector3 relative = nb - na * dot;
            relative = relative.sqrMagnitude > 1e-12f ? relative.normalized : OrthoNormal(na);
            Vector3 dir = na * MathF.Cos(theta) + relative * MathF.Sin(theta);
            return dir * Mathf.LerpUnclamped(magA, magB, t);
        }

        public static Vector3 RotateTowards(Vector3 current, Vector3 target, float maxRadiansDelta, float maxMagnitudeDelta)
        {
            float magC = current.magnitude, magT = target.magnitude;
            if (magC < kEpsilon || magT < kEpsilon)
                return MoveTowards(current, target, maxMagnitudeDelta);
            float angle = Angle(current, target) * Mathf.Deg2Rad;
            float t = angle > 0f ? Mathf.Min(1f, maxRadiansDelta / angle) : 1f;
            Vector3 dir = SlerpUnclamped(current / magC, target / magT, t).normalized;
            return dir * Mathf.MoveTowards(magC, magT, maxMagnitudeDelta);
        }

        public static void OrthoNormalize(ref Vector3 normal, ref Vector3 tangent)
        {
            normal = Normalize(normal);
            tangent = Normalize(ProjectOnPlane(tangent, normal));
            if (tangent.sqrMagnitude < 1e-12f)
                tangent = OrthoNormal(normal);
        }

        private static Vector3 OrthoNormal(Vector3 n) =>
            MathF.Abs(n.x) < 0.9f ? Cross(n, right).normalized : Cross(n, up).normalized;

        public static Vector3 SmoothDamp(Vector3 current, Vector3 target, ref Vector3 currentVelocity, float smoothTime,
                                         float maxSpeed = float.PositiveInfinity, float deltaTime = -1f)
        {
            if (deltaTime < 0f) deltaTime = Time.deltaTime;
            smoothTime = MathF.Max(0.0001f, smoothTime);
            float omega = 2f / smoothTime;
            float x = omega * deltaTime;
            float exp = 1f / (1f + x + 0.48f * x * x + 0.235f * x * x * x);
            Vector3 change = current - target;
            Vector3 originalTo = target;
            change = ClampMagnitude(change, maxSpeed * smoothTime);
            target = current - change;
            Vector3 temp = (currentVelocity + omega * change) * deltaTime;
            currentVelocity = (currentVelocity - omega * temp) * exp;
            Vector3 output = target + (change + temp) * exp;
            if (Dot(originalTo - current, output - originalTo) > 0f)
            {
                output = originalTo;
                currentVelocity = (output - originalTo) / MathF.Max(deltaTime, 1e-6f);
            }
            return output;
        }

        public static Vector3 operator +(Vector3 a, Vector3 b) => new Vector3(a.x + b.x, a.y + b.y, a.z + b.z);
        public static Vector3 operator -(Vector3 a, Vector3 b) => new Vector3(a.x - b.x, a.y - b.y, a.z - b.z);
        public static Vector3 operator -(Vector3 a) => new Vector3(-a.x, -a.y, -a.z);
        public static Vector3 operator *(Vector3 a, float d) => new Vector3(a.x * d, a.y * d, a.z * d);
        public static Vector3 operator *(float d, Vector3 a) => new Vector3(a.x * d, a.y * d, a.z * d);
        public static Vector3 operator /(Vector3 a, float d) => new Vector3(a.x / d, a.y / d, a.z / d);
        public static bool operator ==(Vector3 a, Vector3 b) => (a - b).sqrMagnitude < 9.99999944E-11f;
        public static bool operator !=(Vector3 a, Vector3 b) => !(a == b);

        public bool Equals(Vector3 other) => x.Equals(other.x) && y.Equals(other.y) && z.Equals(other.z);
        public override bool Equals(object other) => other is Vector3 v && Equals(v);
        public override int GetHashCode() => HashCode.Combine(x, y, z);
        public override string ToString() => ToString(null, null);
        public string ToString(string format) => ToString(format, null);
        public string ToString(string format, IFormatProvider provider)
        {
            format ??= "F2";
            provider ??= CultureInfo.InvariantCulture;
            return $"({x.ToString(format, provider)}, {y.ToString(format, provider)}, {z.ToString(format, provider)})";
        }
    }

    [Serializable]
    public struct Vector4 : IEquatable<Vector4>
    {
        public float x;
        public float y;
        public float z;
        public float w;

        public Vector4(float x, float y, float z, float w) { this.x = x; this.y = y; this.z = z; this.w = w; }
        public Vector4(float x, float y, float z) : this(x, y, z, 0f) { }

        public float this[int index]
        {
            get => index switch { 0 => x, 1 => y, 2 => z, 3 => w, _ => throw new IndexOutOfRangeException("Invalid Vector4 index!") };
            set
            {
                switch (index)
                {
                    case 0: x = value; break;
                    case 1: y = value; break;
                    case 2: z = value; break;
                    case 3: w = value; break;
                    default: throw new IndexOutOfRangeException("Invalid Vector4 index!");
                }
            }
        }

        public static Vector4 zero => new Vector4(0f, 0f, 0f, 0f);
        public static Vector4 one => new Vector4(1f, 1f, 1f, 1f);
        public float sqrMagnitude => x * x + y * y + z * z + w * w;
        public float magnitude => MathF.Sqrt(sqrMagnitude);
        public Vector4 normalized { get { float m = magnitude; return m > 1e-5f ? this / m : zero; } }

        public static float Dot(Vector4 a, Vector4 b) => a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
        public static Vector4 Lerp(Vector4 a, Vector4 b, float t) { t = Mathf.Clamp01(t); return a + (b - a) * t; }

        public static Vector4 operator +(Vector4 a, Vector4 b) => new Vector4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w);
        public static Vector4 operator -(Vector4 a, Vector4 b) => new Vector4(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w);
        public static Vector4 operator -(Vector4 a) => new Vector4(-a.x, -a.y, -a.z, -a.w);
        public static Vector4 operator *(Vector4 a, float d) => new Vector4(a.x * d, a.y * d, a.z * d, a.w * d);
        public static Vector4 operator *(float d, Vector4 a) => a * d;
        public static Vector4 operator /(Vector4 a, float d) => new Vector4(a.x / d, a.y / d, a.z / d, a.w / d);
        public static bool operator ==(Vector4 a, Vector4 b) => (a - b).sqrMagnitude < 9.99999944E-11f;
        public static bool operator !=(Vector4 a, Vector4 b) => !(a == b);
        public static implicit operator Vector4(Vector3 v) => new Vector4(v.x, v.y, v.z, 0f);
        public static implicit operator Vector3(Vector4 v) => new Vector3(v.x, v.y, v.z);

        public bool Equals(Vector4 other) => x.Equals(other.x) && y.Equals(other.y) && z.Equals(other.z) && w.Equals(other.w);
        public override bool Equals(object other) => other is Vector4 v && Equals(v);
        public override int GetHashCode() => HashCode.Combine(x, y, z, w);
        public override string ToString() =>
            string.Format(CultureInfo.InvariantCulture, "({0:F2}, {1:F2}, {2:F2}, {3:F2})", x, y, z, w);
    }

    [Serializable]
    public struct Vector2Int : IEquatable<Vector2Int>
    {
        public int x;
        public int y;
        public Vector2Int(int x, int y) { this.x = x; this.y = y; }
        public static Vector2Int zero => new Vector2Int(0, 0);
        public static Vector2Int one => new Vector2Int(1, 1);
        public static Vector2Int operator +(Vector2Int a, Vector2Int b) => new Vector2Int(a.x + b.x, a.y + b.y);
        public static Vector2Int operator -(Vector2Int a, Vector2Int b) => new Vector2Int(a.x - b.x, a.y - b.y);
        public static bool operator ==(Vector2Int a, Vector2Int b) => a.x == b.x && a.y == b.y;
        public static bool operator !=(Vector2Int a, Vector2Int b) => !(a == b);
        public static implicit operator Vector2(Vector2Int v) => new Vector2(v.x, v.y);
        public bool Equals(Vector2Int other) => this == other;
        public override bool Equals(object other) => other is Vector2Int v && this == v;
        public override int GetHashCode() => HashCode.Combine(x, y);
        public override string ToString() => $"({x}, {y})";
    }

    [Serializable]
    public struct Vector3Int : IEquatable<Vector3Int>
    {
        public int x;
        public int y;
        public int z;
        public Vector3Int(int x, int y, int z) { this.x = x; this.y = y; this.z = z; }
        public static Vector3Int zero => new Vector3Int(0, 0, 0);
        public static Vector3Int one => new Vector3Int(1, 1, 1);
        public static Vector3Int operator +(Vector3Int a, Vector3Int b) => new Vector3Int(a.x + b.x, a.y + b.y, a.z + b.z);
        public static Vector3Int operator -(Vector3Int a, Vector3Int b) => new Vector3Int(a.x - b.x, a.y - b.y, a.z - b.z);
        public static bool operator ==(Vector3Int a, Vector3Int b) => a.x == b.x && a.y == b.y && a.z == b.z;
        public static bool operator !=(Vector3Int a, Vector3Int b) => !(a == b);
        public static implicit operator Vector3(Vector3Int v) => new Vector3(v.x, v.y, v.z);
        public bool Equals(Vector3Int other) => this == other;
        public override bool Equals(object other) => other is Vector3Int v && this == v;
        public override int GetHashCode() => HashCode.Combine(x, y, z);
        public override string ToString() => $"({x}, {y}, {z})";
    }
}
