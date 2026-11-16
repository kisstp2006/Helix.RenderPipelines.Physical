using HelixEngine.Builtin;
using HelixEngine.Rendering.ShaderLibrary;
using HelixEngine.Shaders;
using static HelixEngine.Shaders.ShaderMath;

namespace HelixEngine.Rendering.Physical.Shaders;

/// <summary>What the full screen passes take: the triangle's texture coordinate (0 at the image's bottom left).</summary>
public struct FullscreenVaryings
{
    [Position] public float4 position;
    [Location(0)] public float2 uv;
}

/// <summary>
/// Hidden/Physical/Sky: behind the opaques (at the far depth), the skybox's cube along each pixel's view direction
/// (rotated about the up axis, tinted, at its exposure), else the background color, in the environment's luminance and
/// exposed with the frame.
/// </summary>
[Shader("Hidden/Physical/Sky")]
public sealed class Sky : ShaderDefinition
{
    public struct SkyData
    {
        /// <summary>Linear: the cube's tint (doubled at the reference's grey) and the background color.</summary>
        public float4 _SkyTint;
        public float4 _SkyColor;
        /// <summary>x: 1 with a cube, y: its rotation (radians), z: its exposure, w: the environment's luminance.</summary>
        public float4 _SkyParams;
        /// <summary>The projection's P00, P11, P02, P12.</summary>
        public float4 _SkyProjection;
        public float4 _SkyRight;
        public float4 _SkyUp;
        public float4 _SkyForward;
    }

    [Uniform(0)] public static SkyData sky;
    [Texture(0)] public static TextureCube<float4> _SkyCube;
    [Sampler(0)] public static SamplerState sampler_SkyCube;

    [Pass("Sky", Cull = CullMode.Off, ZTest = CompareFunction.LessEqual, ZWrite = false)]
    public static class Draw
    {
        [VertexShader]
        public static FullscreenVaryings Vertex([VertexId] uint id)
        {
            FullscreenVaryings o;
            o.position = Sampling.GetFullScreenTriangleVertexPosition(id, 1.0f);
            o.uv = Sampling.GetFullScreenTriangleTexCoord(id);
            return o;
        }

        [FragmentShader]
        [return: Target(0)]
        public static float4 Fragment(FullscreenVaryings input)
        {
            float2 ndc = input.uv * 2.0f - new float2(1.0f);
            float4 p = sky._SkyProjection;
            float3 direction = Normalize(sky._SkyForward.xyz + sky._SkyRight.xyz * ((ndc.x + p.z) / p.x) + sky._SkyUp.xyz * ((ndc.y + p.w) / p.y));
            float3 color = sky._SkyColor.xyz;
            if (sky._SkyParams.x > 0.5f)
            {
                float sinR = Sin(sky._SkyParams.y), cosR = Cos(sky._SkyParams.y);
                float3 rotated = new float3(direction.x * cosR - direction.z * sinR, direction.y, direction.x * sinR + direction.z * cosR);
                color = _SkyCube.SampleLevel(sampler_SkyCube, rotated, 0.0f).xyz * sky._SkyTint.xyz * sky._SkyParams.z;
            }
            return new float4(color * sky._SkyParams.w * PhysicalGlobals.Exposure(), 1.0f);
        }
    }
}

/// <summary>
/// Hidden/Physical/TemporalAA: the current (jittered) frame blended with the previous resolve, which is reprojected by
/// the camera's motion (each pixel's world position from its depth seen by the previous view and projection) and
/// clamped to the box of the current frame's 3 x 3 neighbourhood in YCoCg. A new history (or one from outside the
/// previous view) takes the frame alone.
/// </summary>
[Shader("Hidden/Physical/TemporalAA")]
public sealed class TemporalAA : ShaderDefinition
{
    public struct TaaData
    {
        public float4x4 _TaaInverseViewProjection;
        public float4x4 _TaaPreviousViewProjection;
        /// <summary>x: the history's weight, y: 1 for a new history, zw: the jitter in texture coordinates.</summary>
        public float4 _TaaParams;
        /// <summary>xy: the size, zw: one over it.</summary>
        public float4 _TaaScreen;
    }

    [Uniform(0)] public static TaaData taa;
    [Texture(0)] public static Texture2D<float4> _TaaColor;
    [Sampler(0)] public static SamplerState sampler_TaaColor;
    [Texture(1)] public static Texture2D<float4> _TaaHistory;
    [Sampler(1)] public static SamplerState sampler_TaaHistory;
    [Texture(2)] public static Texture2D<float> _TaaDepth;

    static float3 ToYCoCg(float3 c) => new float3(Dot(c, new float3(0.25f, 0.5f, 0.25f)), Dot(c, new float3(0.5f, 0.0f, -0.5f)), Dot(c, new float3(-0.25f, 0.5f, -0.25f)));

    static float3 FromYCoCg(float3 c) => new float3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);

    [Pass("Resolve", Cull = CullMode.Off, ZTest = CompareFunction.Always, ZWrite = false)]
    public static class Resolve
    {
        [VertexShader]
        public static FullscreenVaryings Vertex([VertexId] uint id)
        {
            FullscreenVaryings o;
            o.position = Sampling.GetFullScreenTriangleVertexPosition(id);
            o.uv = Sampling.GetFullScreenTriangleTexCoord(id);
            return o;
        }

        [FragmentShader]
        [return: Target(0)]
        public static float4 Fragment(FullscreenVaryings input)
        {
            int2 size = _TaaColor.Size;
            int2 texel = new int2(Clamp((int)Floor(input.uv.x * (float)size.x), 0, size.x - 1), Clamp((int)Floor(input.uv.y * (float)size.y), 0, size.y - 1));
            float4 current = _TaaColor.Load(texel, 0);
            if (taa._TaaParams.y > 0.5f)
                return current;
            // The neighbourhood's box.
            float3 lo = new float3(1e30f), hi = new float3(-1e30f);
            for (int y = -1; y <= 1; y++)
                for (int x = -1; x <= 1; x++)
                {
                    int2 t = new int2(Clamp(texel.x + x, 0, size.x - 1), Clamp(texel.y + y, 0, size.y - 1));
                    float3 c = ToYCoCg(_TaaColor.Load(t, 0).xyz);
                    lo = Min(lo, c);
                    hi = Max(hi, c);
                }
            // Where the pixel was in the previous frame.
            float depth = _TaaDepth.Load(texel, 0);
            float2 uv = (new float2(texel.x, texel.y) + new float2(0.5f)) * taa._TaaScreen.zw;
            float4 world = Mul(taa._TaaInverseViewProjection, new float4(uv * 2.0f - new float2(1.0f), depth, 1.0f));
            world /= world.w;
            float4 previous = Mul(taa._TaaPreviousViewProjection, new float4(world.xyz, 1.0f));
            float2 previousUV = previous.xy / previous.w * 0.5f + new float2(0.5f);
            if (previousUV.x < 0.0f || previousUV.y < 0.0f || previousUV.x > 1.0f || previousUV.y > 1.0f)
                return current;
            float3 history = ToYCoCg(_TaaHistory.SampleLevel(sampler_TaaHistory, previousUV, 0.0f).xyz);
            history = FromYCoCg(Clamp(history, lo, hi));
            return new float4(Lerp(current.xyz, history, taa._TaaParams.x), current.w);
        }
    }
}

/// <summary>Hidden/Physical/Final: the exposed frame tonemapped (none, Neutral, ACES) and encoded for the display (sRGB).</summary>
[Shader("Hidden/Physical/Final")]
public sealed class Final : ShaderDefinition
{
    public struct FinalData
    {
        /// <summary>x: the tonemapping mode (0 none, 1 Neutral, 2 ACES).</summary>
        public float4 _FinalParams;
    }

    [Uniform(0)] public static FinalData final;
    [Texture(0)] public static Texture2D<float4> _FinalSource;

    [Pass("Final", Cull = CullMode.Off, ZTest = CompareFunction.Always, ZWrite = false)]
    public static class Draw
    {
        [VertexShader]
        public static FullscreenVaryings Vertex([VertexId] uint id)
        {
            FullscreenVaryings o;
            o.position = Sampling.GetFullScreenTriangleVertexPosition(id);
            o.uv = Sampling.GetFullScreenTriangleTexCoord(id);
            return o;
        }

        [FragmentShader]
        [return: Target(0)]
        public static float4 Fragment(FullscreenVaryings input)
        {
            int2 size = _FinalSource.Size;
            int2 texel = new int2(Clamp((int)Floor(input.uv.x * (float)size.x), 0, size.x - 1), Clamp((int)Floor(input.uv.y * (float)size.y), 0, size.y - 1));
            float3 c = Max(_FinalSource.Load(texel, 0).xyz, new float3(0.0f));
            float mode = final._FinalParams.x;
            if (mode > 1.5f)
                c = ACES.AcesTonemap(ACES.UnityToACES(c));
            else if (mode > 0.5f)
                c = ColorGrading.NeutralTonemap(c);
            return new float4(Color.LinearToSRGB(Saturate(c)), 1.0f);
        }
    }
}
