using System;
using System.Globalization;

namespace UnityEngine
{
    public static class Mathf
    {
        public const float PI = MathF.PI;
        public const float Infinity = float.PositiveInfinity;
        public const float NegativeInfinity = float.NegativeInfinity;
        public const float Deg2Rad = PI / 180f;
        public const float Rad2Deg = 180f / PI;
        public static readonly float Epsilon = float.Epsilon;

        public static float Sin(float f) => MathF.Sin(f);
        public static float Cos(float f) => MathF.Cos(f);
        public static float Tan(float f) => MathF.Tan(f);
        public static float Asin(float f) => MathF.Asin(f);
        public static float Acos(float f) => MathF.Acos(f);
        public static float Atan(float f) => MathF.Atan(f);
        public static float Atan2(float y, float x) => MathF.Atan2(y, x);
        public static float Sqrt(float f) => MathF.Sqrt(f);
        public static float Abs(float f) => MathF.Abs(f);
        public static int Abs(int value) => Math.Abs(value);
        public static float Min(float a, float b) => a < b ? a : b;
        public static float Min(params float[] values)
        {
            if (values.Length == 0) return 0f;
            float m = values[0];
            for (int i = 1; i < values.Length; i++) if (values[i] < m) m = values[i];
            return m;
        }
        public static int Min(int a, int b) => a < b ? a : b;
        public static int Min(params int[] values)
        {
            if (values.Length == 0) return 0;
            int m = values[0];
            for (int i = 1; i < values.Length; i++) if (values[i] < m) m = values[i];
            return m;
        }
        public static float Max(float a, float b) => a > b ? a : b;
        public static float Max(params float[] values)
        {
            if (values.Length == 0) return 0f;
            float m = values[0];
            for (int i = 1; i < values.Length; i++) if (values[i] > m) m = values[i];
            return m;
        }
        public static int Max(int a, int b) => a > b ? a : b;
        public static int Max(params int[] values)
        {
            if (values.Length == 0) return 0;
            int m = values[0];
            for (int i = 1; i < values.Length; i++) if (values[i] > m) m = values[i];
            return m;
        }
        public static float Pow(float f, float p) => MathF.Pow(f, p);
        public static float Exp(float power) => MathF.Exp(power);
        public static float Log(float f, float p) => MathF.Log(f) / MathF.Log(p);
        public static float Log(float f) => MathF.Log(f);
        public static float Log10(float f) => MathF.Log10(f);
        public static float Ceil(float f) => MathF.Ceiling(f);
        public static float Floor(float f) => MathF.Floor(f);
        public static float Round(float f) => MathF.Round(f, MidpointRounding.ToEven);
        public static int CeilToInt(float f) => (int)MathF.Ceiling(f);
        public static int FloorToInt(float f) => (int)MathF.Floor(f);
        public static int RoundToInt(float f) => (int)MathF.Round(f, MidpointRounding.ToEven);
        public static float Sign(float f) => f >= 0f ? 1f : -1f;

        public static float Clamp(float value, float min, float max) => value < min ? min : value > max ? max : value;
        public static int Clamp(int value, int min, int max) => value < min ? min : value > max ? max : value;
        public static float Clamp01(float value) => value < 0f ? 0f : value > 1f ? 1f : value;

        public static float Lerp(float a, float b, float t) => a + (b - a) * Clamp01(t);
        public static float LerpUnclamped(float a, float b, float t) => a + (b - a) * t;
        public static float InverseLerp(float a, float b, float value) => a != b ? Clamp01((value - a) / (b - a)) : 0f;

        public static float LerpAngle(float a, float b, float t)
        {
            float delta = Repeat(b - a, 360f);
            if (delta > 180f) delta -= 360f;
            return a + delta * Clamp01(t);
        }

        public static float MoveTowards(float current, float target, float maxDelta) =>
            Abs(target - current) <= maxDelta ? target : current + Sign(target - current) * maxDelta;

        public static float MoveTowardsAngle(float current, float target, float maxDelta)
        {
            float delta = DeltaAngle(current, target);
            if (-maxDelta < delta && delta < maxDelta)
                return target;
            return MoveTowards(current, current + delta, maxDelta);
        }

        public static float SmoothStep(float from, float to, float t)
        {
            t = Clamp01(t);
            t = -2f * t * t * t + 3f * t * t;
            return to * t + from * (1f - t);
        }

        public static float Repeat(float t, float length) => Clamp(t - Floor(t / length) * length, 0f, length);

        public static float PingPong(float t, float length)
        {
            t = Repeat(t, length * 2f);
            return length - Abs(t - length);
        }

        public static float DeltaAngle(float current, float target)
        {
            float delta = Repeat(target - current, 360f);
            if (delta > 180f) delta -= 360f;
            return delta;
        }

        public static bool Approximately(float a, float b) =>
            Abs(b - a) < Max(1E-06f * Max(Abs(a), Abs(b)), Epsilon * 8f);

        public static float SmoothDamp(float current, float target, ref float currentVelocity, float smoothTime) =>
            SmoothDamp(current, target, ref currentVelocity, smoothTime, Infinity, Time.deltaTime);

        public static float SmoothDamp(float current, float target, ref float currentVelocity, float smoothTime, float maxSpeed) =>
            SmoothDamp(current, target, ref currentVelocity, smoothTime, maxSpeed, Time.deltaTime);

        public static float SmoothDamp(float current, float target, ref float currentVelocity, float smoothTime,
                                       float maxSpeed, float deltaTime)
        {
            smoothTime = Max(0.0001f, smoothTime);
            float omega = 2f / smoothTime;
            float x = omega * deltaTime;
            float exp = 1f / (1f + x + 0.48f * x * x + 0.235f * x * x * x);
            float change = current - target;
            float originalTo = target;
            float maxChange = maxSpeed * smoothTime;
            change = Clamp(change, -maxChange, maxChange);
            target = current - change;
            float temp = (currentVelocity + omega * change) * deltaTime;
            currentVelocity = (currentVelocity - omega * temp) * exp;
            float output = target + (change + temp) * exp;
            if (originalTo - current > 0f == output > originalTo)
            {
                output = originalTo;
                currentVelocity = (output - originalTo) / Max(deltaTime, 1e-6f);
            }
            return output;
        }

        public static float SmoothDampAngle(float current, float target, ref float currentVelocity, float smoothTime,
                                            float maxSpeed = Infinity, float deltaTime = -1f)
        {
            if (deltaTime < 0f) deltaTime = Time.deltaTime;
            target = current + DeltaAngle(current, target);
            return SmoothDamp(current, target, ref currentVelocity, smoothTime, maxSpeed, deltaTime);
        }

        public static bool IsPowerOfTwo(int value) => value > 0 && (value & (value - 1)) == 0;

        public static int NextPowerOfTwo(int value)
        {
            if (value <= 1) return 1;
            value--;
            value |= value >> 1; value |= value >> 2; value |= value >> 4; value |= value >> 8; value |= value >> 16;
            return value + 1;
        }

        public static float PerlinNoise(float x, float y) => Noise.Perlin(x, y);
    }

    internal static class Noise
    {
        private static readonly int[] P = BuildPermutation();

        private static int[] BuildPermutation()
        {
            int[] basePerm =
            {
                151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,8,99,37,240,21,10,23,
                190,6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,57,177,33,88,237,149,56,87,174,20,
                125,136,171,168,68,175,74,165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,60,211,133,230,220,
                105,92,41,55,46,245,40,244,102,143,54,65,25,63,161,1,216,80,73,209,76,132,187,208,89,18,169,200,196,
                135,130,116,188,159,86,164,100,109,198,173,186,3,64,52,217,226,250,124,123,5,202,38,147,118,126,255,
                82,85,212,207,206,59,227,47,16,58,17,182,189,28,42,223,183,170,213,119,248,152,2,44,154,163,70,221,
                153,101,155,167,43,172,9,129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,218,246,97,228,
                251,34,242,193,238,210,144,12,191,179,162,241,81,51,145,235,249,14,239,107,49,192,214,31,181,199,106,
                157,184,84,204,176,115,121,50,45,127,4,150,254,138,236,205,93,222,114,67,29,24,72,243,141,128,195,78,
                66,215,61,156,180
            };
            int[] p = new int[512];
            for (int i = 0; i < 512; i++) p[i] = basePerm[i & 255];
            return p;
        }

        private static float Fade(float t) => t * t * t * (t * (t * 6f - 15f) + 10f);

        private static float Grad(int hash, float x, float y)
        {
            int h = hash & 7;
            float u = h < 4 ? x : y;
            float v = h < 4 ? y : x;
            return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
        }

        public static float Perlin(float x, float y)
        {
            int xi = (int)MathF.Floor(x) & 255, yi = (int)MathF.Floor(y) & 255;
            float xf = x - MathF.Floor(x), yf = y - MathF.Floor(y);
            float u = Fade(xf), v = Fade(yf);
            int aa = P[P[xi] + yi], ab = P[P[xi] + yi + 1], ba = P[P[xi + 1] + yi], bb = P[P[xi + 1] + yi + 1];
            float x1 = Mathf.LerpUnclamped(Grad(aa, xf, yf), Grad(ba, xf - 1f, yf), u);
            float x2 = Mathf.LerpUnclamped(Grad(ab, xf, yf - 1f), Grad(bb, xf - 1f, yf - 1f), u);
            return Mathf.Clamp01((Mathf.LerpUnclamped(x1, x2, v) + 1f) * 0.5f);
        }
    }

    [Serializable]
    public struct Color : IEquatable<Color>
    {
        public float r;
        public float g;
        public float b;
        public float a;

        public Color(float r, float g, float b, float a) { this.r = r; this.g = g; this.b = b; this.a = a; }
        public Color(float r, float g, float b) : this(r, g, b, 1f) { }

        public static Color red => new Color(1f, 0f, 0f, 1f);
        public static Color green => new Color(0f, 1f, 0f, 1f);
        public static Color blue => new Color(0f, 0f, 1f, 1f);
        public static Color white => new Color(1f, 1f, 1f, 1f);
        public static Color black => new Color(0f, 0f, 0f, 1f);
        public static Color yellow => new Color(1f, 0.92156863f, 0.015686275f, 1f);
        public static Color cyan => new Color(0f, 1f, 1f, 1f);
        public static Color magenta => new Color(1f, 0f, 1f, 1f);
        public static Color gray => new Color(0.5f, 0.5f, 0.5f, 1f);
        public static Color grey => gray;
        public static Color clear => new Color(0f, 0f, 0f, 0f);

        public float grayscale => 0.299f * r + 0.587f * g + 0.114f * b;
        public float maxColorComponent => MathF.Max(MathF.Max(r, g), b);

        public float this[int index]
        {
            get => index switch { 0 => r, 1 => g, 2 => b, 3 => a, _ => throw new IndexOutOfRangeException("Invalid Color index!") };
            set
            {
                switch (index)
                {
                    case 0: r = value; break;
                    case 1: g = value; break;
                    case 2: b = value; break;
                    case 3: a = value; break;
                    default: throw new IndexOutOfRangeException("Invalid Color index!");
                }
            }
        }

        public static Color Lerp(Color a, Color b, float t) { t = Mathf.Clamp01(t); return LerpUnclamped(a, b, t); }
        public static Color LerpUnclamped(Color a, Color b, float t) =>
            new Color(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);

        public static Color HSVToRGB(float h, float s, float v)
        {
            if (s == 0f) return new Color(v, v, v, 1f);
            h = Mathf.Repeat(h, 1f) * 6f;
            int sector = (int)MathF.Floor(h);
            float f = h - sector;
            float p = v * (1f - s), q = v * (1f - s * f), t = v * (1f - s * (1f - f));
            return sector switch
            {
                0 => new Color(v, t, p),
                1 => new Color(q, v, p),
                2 => new Color(p, v, t),
                3 => new Color(p, q, v),
                4 => new Color(t, p, v),
                _ => new Color(v, p, q),
            };
        }

        public static void RGBToHSV(Color rgb, out float h, out float s, out float v)
        {
            float max = rgb.maxColorComponent, min = MathF.Min(MathF.Min(rgb.r, rgb.g), rgb.b);
            v = max;
            float d = max - min;
            s = max > 0f ? d / max : 0f;
            if (d <= 0f) { h = 0f; return; }
            if (max == rgb.r) h = (rgb.g - rgb.b) / d + (rgb.g < rgb.b ? 6f : 0f);
            else if (max == rgb.g) h = (rgb.b - rgb.r) / d + 2f;
            else h = (rgb.r - rgb.g) / d + 4f;
            h /= 6f;
        }

        public static Color operator +(Color a, Color b) => new Color(a.r + b.r, a.g + b.g, a.b + b.b, a.a + b.a);
        public static Color operator -(Color a, Color b) => new Color(a.r - b.r, a.g - b.g, a.b - b.b, a.a - b.a);
        public static Color operator *(Color a, Color b) => new Color(a.r * b.r, a.g * b.g, a.b * b.b, a.a * b.a);
        public static Color operator *(Color a, float b) => new Color(a.r * b, a.g * b, a.b * b, a.a * b);
        public static Color operator *(float b, Color a) => a * b;
        public static Color operator /(Color a, float b) => new Color(a.r / b, a.g / b, a.b / b, a.a / b);
        public static bool operator ==(Color a, Color b) => (Vector4)a == (Vector4)b;
        public static bool operator !=(Color a, Color b) => !(a == b);
        public static implicit operator Vector4(Color c) => new Vector4(c.r, c.g, c.b, c.a);
        public static implicit operator Color(Vector4 v) => new Color(v.x, v.y, v.z, v.w);

        public bool Equals(Color other) => r.Equals(other.r) && g.Equals(other.g) && b.Equals(other.b) && a.Equals(other.a);
        public override bool Equals(object other) => other is Color c && Equals(c);
        public override int GetHashCode() => HashCode.Combine(r, g, b, a);
        public override string ToString() =>
            string.Format(CultureInfo.InvariantCulture, "RGBA({0:F3}, {1:F3}, {2:F3}, {3:F3})", r, g, b, a);
    }

    [Serializable]
    public struct Color32 : IEquatable<Color32>
    {
        public byte r;
        public byte g;
        public byte b;
        public byte a;

        public Color32(byte r, byte g, byte b, byte a) { this.r = r; this.g = g; this.b = b; this.a = a; }

        public static implicit operator Color(Color32 c) => new Color(c.r / 255f, c.g / 255f, c.b / 255f, c.a / 255f);

        public static implicit operator Color32(Color c) => new Color32(
            (byte)Mathf.RoundToInt(Mathf.Clamp01(c.r) * 255f), (byte)Mathf.RoundToInt(Mathf.Clamp01(c.g) * 255f),
            (byte)Mathf.RoundToInt(Mathf.Clamp01(c.b) * 255f), (byte)Mathf.RoundToInt(Mathf.Clamp01(c.a) * 255f));

        public bool Equals(Color32 other) => r == other.r && g == other.g && b == other.b && a == other.a;
        public override bool Equals(object other) => other is Color32 c && Equals(c);
        public override int GetHashCode() => HashCode.Combine(r, g, b, a);
        public override string ToString() => $"RGBA({r}, {g}, {b}, {a})";
    }

    public static class ColorUtility
    {
        public static bool TryParseHtmlString(string htmlString, out Color color)
        {
            color = Color.white;
            if (string.IsNullOrEmpty(htmlString)) return false;
            string s = htmlString.TrimStart('#');
            if (!uint.TryParse(s, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out uint v)) return false;
            switch (s.Length)
            {
                case 6: color = new Color32((byte)(v >> 16), (byte)(v >> 8), (byte)v, 255); return true;
                case 8: color = new Color32((byte)(v >> 24), (byte)(v >> 16), (byte)(v >> 8), (byte)v); return true;
                default: return false;
            }
        }

        public static string ToHtmlStringRGB(Color color)
        {
            Color32 c = color;
            return $"{c.r:X2}{c.g:X2}{c.b:X2}";
        }

        public static string ToHtmlStringRGBA(Color color)
        {
            Color32 c = color;
            return $"{c.r:X2}{c.g:X2}{c.b:X2}{c.a:X2}";
        }
    }

    public static class Random
    {
        private static System.Random rng = new System.Random();

        public static void InitState(int seed) => rng = new System.Random(seed);
        public static float value => (float)rng.NextDouble();
        public static float Range(float minInclusive, float maxInclusive) => minInclusive + (maxInclusive - minInclusive) * value;
        public static int Range(int minInclusive, int maxExclusive) =>
            maxExclusive > minInclusive ? rng.Next(minInclusive, maxExclusive) : minInclusive;

        public static Vector3 insideUnitSphere
        {
            get
            {
                Vector3 v;
                do v = new Vector3(Range(-1f, 1f), Range(-1f, 1f), Range(-1f, 1f));
                while (v.sqrMagnitude > 1f);
                return v;
            }
        }

        public static Vector2 insideUnitCircle
        {
            get
            {
                Vector2 v;
                do v = new Vector2(Range(-1f, 1f), Range(-1f, 1f));
                while (v.sqrMagnitude > 1f);
                return v;
            }
        }

        public static Vector3 onUnitSphere
        {
            get
            {
                Vector3 v;
                do v = insideUnitSphere;
                while (v.sqrMagnitude < 1e-6f);
                return v.normalized;
            }
        }

        public static Quaternion rotation => Quaternion.Euler(Range(0f, 360f), Range(0f, 360f), Range(0f, 360f));

        public static Color ColorHSV() => Color.HSVToRGB(value, value, value);
        public static Color ColorHSV(float hueMin, float hueMax, float saturationMin = 0f, float saturationMax = 1f,
                                     float valueMin = 0f, float valueMax = 1f) =>
            Color.HSVToRGB(Range(hueMin, hueMax), Range(saturationMin, saturationMax), Range(valueMin, valueMax));
    }

    [Serializable]
    public struct Ray
    {
        private Vector3 m_Origin;
        private Vector3 m_Direction;

        public Ray(Vector3 origin, Vector3 direction) { m_Origin = origin; m_Direction = direction.normalized; }

        public Vector3 origin { get => m_Origin; set => m_Origin = value; }
        public Vector3 direction { get => m_Direction; set => m_Direction = value.normalized; }
        public Vector3 GetPoint(float distance) => m_Origin + m_Direction * distance;
        public override string ToString() => $"Origin: {m_Origin}, Dir: {m_Direction}";
    }

    [Serializable]
    public struct Bounds : IEquatable<Bounds>
    {
        private Vector3 m_Center;
        private Vector3 m_Extents;

        public Bounds(Vector3 center, Vector3 size) { m_Center = center; m_Extents = size * 0.5f; }

        public Vector3 center { get => m_Center; set => m_Center = value; }
        public Vector3 size { get => m_Extents * 2f; set => m_Extents = value * 0.5f; }
        public Vector3 extents { get => m_Extents; set => m_Extents = value; }
        public Vector3 min { get => m_Center - m_Extents; set => SetMinMax(value, max); }
        public Vector3 max { get => m_Center + m_Extents; set => SetMinMax(min, value); }

        public void SetMinMax(Vector3 min, Vector3 max) { m_Extents = (max - min) * 0.5f; m_Center = min + m_Extents; }
        public void Encapsulate(Vector3 point) => SetMinMax(Vector3.Min(min, point), Vector3.Max(max, point));
        public void Encapsulate(Bounds bounds) { Encapsulate(bounds.min); Encapsulate(bounds.max); }
        public void Expand(float amount) => m_Extents += Vector3.one * (amount * 0.5f);
        public void Expand(Vector3 amount) => m_Extents += amount * 0.5f;

        public bool Contains(Vector3 p) =>
            p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y && p.z >= min.z && p.z <= max.z;

        public bool Intersects(Bounds b) =>
            min.x <= b.max.x && max.x >= b.min.x && min.y <= b.max.y && max.y >= b.min.y && min.z <= b.max.z && max.z >= b.min.z;

        public Vector3 ClosestPoint(Vector3 p) => Vector3.Min(Vector3.Max(p, min), max);
        public float SqrDistance(Vector3 p) => (ClosestPoint(p) - p).sqrMagnitude;

        public static bool operator ==(Bounds a, Bounds b) => a.center == b.center && a.extents == b.extents;
        public static bool operator !=(Bounds a, Bounds b) => !(a == b);
        public bool Equals(Bounds other) => m_Center.Equals(other.m_Center) && m_Extents.Equals(other.m_Extents);
        public override bool Equals(object other) => other is Bounds b && Equals(b);
        public override int GetHashCode() => HashCode.Combine(m_Center, m_Extents);
        public override string ToString() => $"Center: {m_Center}, Extents: {m_Extents}";
    }

    [Serializable]
    public struct Rect : IEquatable<Rect>
    {
        public float x;
        public float y;
        public float width;
        public float height;

        public Rect(float x, float y, float width, float height) { this.x = x; this.y = y; this.width = width; this.height = height; }
        public Rect(Vector2 position, Vector2 size) : this(position.x, position.y, size.x, size.y) { }

        public static Rect zero => new Rect(0f, 0f, 0f, 0f);
        public Vector2 position { get => new Vector2(x, y); set { x = value.x; y = value.y; } }
        public Vector2 size { get => new Vector2(width, height); set { width = value.x; height = value.y; } }
        public Vector2 center => new Vector2(x + width * 0.5f, y + height * 0.5f);
        public Vector2 min => new Vector2(xMin, yMin);
        public Vector2 max => new Vector2(xMax, yMax);
        public float xMin { get => MathF.Min(x, x + width); set { float oldMax = xMax; x = value; width = oldMax - x; } }
        public float yMin { get => MathF.Min(y, y + height); set { float oldMax = yMax; y = value; height = oldMax - y; } }
        public float xMax { get => MathF.Max(x, x + width); set => width = value - x; }
        public float yMax { get => MathF.Max(y, y + height); set => height = value - y; }

        public bool Contains(Vector2 p) => p.x >= xMin && p.x < xMax && p.y >= yMin && p.y < yMax;
        public bool Overlaps(Rect other) => other.xMax > xMin && other.xMin < xMax && other.yMax > yMin && other.yMin < yMax;

        public static bool operator ==(Rect a, Rect b) => a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
        public static bool operator !=(Rect a, Rect b) => !(a == b);
        public bool Equals(Rect other) => this == other;
        public override bool Equals(object other) => other is Rect r && this == r;
        public override int GetHashCode() => HashCode.Combine(x, y, width, height);
        public override string ToString() =>
            string.Format(CultureInfo.InvariantCulture, "(x:{0:F2}, y:{1:F2}, width:{2:F2}, height:{3:F2})", x, y, width, height);
    }
}
