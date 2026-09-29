using System;
using System.Text;

namespace UnityEngine
{
    public enum FilterMode { Point = 0, Bilinear = 1, Trilinear = 2 }
    public enum TextureWrapMode { Repeat = 0, Clamp = 1, Mirror = 2, MirrorOnce = 3 }

    public enum TextureFormat
    {
        Alpha8 = 1, RGB24 = 3, RGBA32 = 4, ARGB32 = 5, RGB565 = 7, R16 = 9, DXT1 = 10, DXT5 = 12,
        RGBAHalf = 17, RGBAFloat = 20, BC7 = 25, R8 = 63, RG16 = 62,
    }

    /// <summary>
    /// Texture assets. Scripts get them from Resources.Load and serialized fields; their pixels
    /// stay with the renderer, so pixel access works on textures created from scripts only.
    /// </summary>
    public class Texture : Object
    {
        /// <summary>Asset path relative to Assets ("" for textures created at runtime).</summary>
        internal string m_AssetPath = string.Empty;

        public virtual int width { get; set; }
        public virtual int height { get; set; }
        public FilterMode filterMode { get; set; } = FilterMode.Bilinear;
        public TextureWrapMode wrapMode { get; set; } = TextureWrapMode.Repeat;
        public int anisoLevel { get; set; } = 1;
        public int mipmapCount => 1;
    }

    public class Texture2D : Texture
    {
        private Color[] m_Pixels;

        internal Texture2D() { }

        public Texture2D(int width, int height) : this(width, height, TextureFormat.RGBA32, true) { }

        public Texture2D(int width, int height, TextureFormat textureFormat, bool mipChain)
        {
            this.width = Math.Max(1, width);
            this.height = Math.Max(1, height);
            format = textureFormat;
            m_Pixels = new Color[this.width * this.height];
        }

        public TextureFormat format { get; private set; } = TextureFormat.RGBA32;
        public bool isReadable => m_Pixels != null;

        private static Texture2D s_White, s_Black;
        public static Texture2D whiteTexture => s_White ??= Solid("UnityWhite", Color.white);
        public static Texture2D blackTexture => s_Black ??= Solid("UnityBlack", Color.black);

        private static Texture2D Solid(string name, Color color)
        {
            var t = new Texture2D(4, 4) { name = name };
            Array.Fill(t.m_Pixels, color);
            return t;
        }

        private Color[] Pixels => m_Pixels ??= new Color[Math.Max(1, width) * Math.Max(1, height)];

        public void SetPixel(int x, int y, Color color)
        {
            if (x >= 0 && y >= 0 && x < width && y < height)
                Pixels[y * width + x] = color;
        }

        public Color GetPixel(int x, int y) =>
            x >= 0 && y >= 0 && x < width && y < height ? Pixels[y * width + x] : Color.clear;

        public Color GetPixelBilinear(float u, float v) =>
            GetPixel(Mathf.Clamp((int)(u * width), 0, width - 1), Mathf.Clamp((int)(v * height), 0, height - 1));

        public Color[] GetPixels() => (Color[])Pixels.Clone();

        public void SetPixels(Color[] colors)
        {
            if (colors != null)
                Array.Copy(colors, Pixels, Math.Min(colors.Length, Pixels.Length));
        }

        public void Apply() { }
        public void Apply(bool updateMipmaps) { }
        public void Apply(bool updateMipmaps, bool makeNoLongerReadable) { }

        public bool Reinitialize(int width, int height)
        {
            this.width = Math.Max(1, width);
            this.height = Math.Max(1, height);
            m_Pixels = new Color[this.width * this.height];
            return true;
        }
    }

    public enum SpriteMeshType { FullRect = 0, Tight = 1 }

    /// <summary>A sprite: a rectangle of a texture (a whole image or one entry of a sprite sheet).</summary>
    public class Sprite : Object
    {
        public Texture2D texture { get; internal set; }
        public Rect rect { get; internal set; }
        public Rect textureRect => rect;
        public Vector2 pivot { get; internal set; }
        public float pixelsPerUnit { get; internal set; } = 100f;
        public Vector4 border { get; internal set; }

        public Bounds bounds =>
            new Bounds(Vector3.zero, new Vector3(rect.width / pixelsPerUnit, rect.height / pixelsPerUnit, 0f));

        public static Sprite Create(Texture2D texture, Rect rect, Vector2 pivot) => Create(texture, rect, pivot, 100f);

        public static Sprite Create(Texture2D texture, Rect rect, Vector2 pivot, float pixelsPerUnit) =>
            new Sprite { texture = texture, rect = rect, pivot = pivot, pixelsPerUnit = pixelsPerUnit, name = texture ? texture.name : "Sprite" };

        public static Sprite Create(Texture2D texture, Rect rect, Vector2 pivot, float pixelsPerUnit, uint extrude,
                                    SpriteMeshType meshType) => Create(texture, rect, pivot, pixelsPerUnit);

        public static Sprite Create(Texture2D texture, Rect rect, Vector2 pivot, float pixelsPerUnit, uint extrude,
                                    SpriteMeshType meshType, Vector4 border) =>
            new Sprite { texture = texture, rect = rect, pivot = pivot, pixelsPerUnit = pixelsPerUnit, border = border, name = texture ? texture.name : "Sprite" };
    }

    /// <summary>Audio clip asset (playback comes with the audio system).</summary>
    public class AudioClip : Object
    {
        internal string m_AssetPath = string.Empty;

        public float length { get; internal set; }
        public int samples { get; internal set; }
        public int channels { get; internal set; } = 2;
        public int frequency { get; internal set; } = 44100;
        public bool LoadAudioData() => true;
        public bool UnloadAudioData() => true;
    }

    /// <summary>Text or binary file asset (.txt, .json, .bytes, .csv, .xml ...).</summary>
    public class TextAsset : Object
    {
        private readonly byte[] m_Bytes;

        public TextAsset() : this(string.Empty) { }

        public TextAsset(string text)
        {
            m_Bytes = Encoding.UTF8.GetBytes(text ?? string.Empty);
        }

        internal TextAsset(byte[] bytes)
        {
            m_Bytes = bytes ?? Array.Empty<byte>();
        }

        public string text
        {
            get
            {
                // Skip a UTF-8 byte order mark, like Unity.
                int start = m_Bytes.Length >= 3 && m_Bytes[0] == 0xEF && m_Bytes[1] == 0xBB && m_Bytes[2] == 0xBF ? 3 : 0;
                return Encoding.UTF8.GetString(m_Bytes, start, m_Bytes.Length - start);
            }
        }

        public byte[] bytes => (byte[])m_Bytes.Clone();
        public long dataSize => m_Bytes.Length;

        public override string ToString() => text;
    }
}
