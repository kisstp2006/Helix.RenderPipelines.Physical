using HelixEngine.Builtin;
using HelixEngine.Rendering.ShaderLibrary;
using HelixEngine.Shaders;
using static HelixEngine.Shaders.ShaderMath;

namespace HelixEngine.Rendering.Physical.Shaders;

/// <summary>
/// Physical/Unlit: a color (sRGB) times a map, emitted at a luminance in nits and exposed with the frame; opaque, or
/// blended as the material's properties say. Passes: PhysicalForward, PhysicalDepthNormals, ShadowCaster.
/// </summary>
[Shader("Physical/Unlit")]
[Tag("RenderType", "Opaque")]
[Tag("RenderPipeline", "PhysicalPipeline")]
public sealed class Unlit : ShaderDefinition
{
    [Property("Color"), MainColor, Default(1.0f, 1.0f, 1.0f, 1.0f)] public float4 _UnlitColor;
    [Property("Color Map"), MainTexture, DefaultTexture("white")] public Texture2D<float4> _UnlitColorMap;
    [Property("Luminance (nits)"), Default(100.0f)] public float _UnlitLuminance;
    [Property("Cull Mode"), Default(2.0f)] public float _Cull;
    [Property("Src Blend"), Default(1.0f)] public float _SrcBlend;
    [Property("Dst Blend"), Default(0.0f)] public float _DstBlend;
    [Property("ZWrite"), Default(1.0f)] public float _ZWrite;

    public struct MaterialData
    {
        public float4 _UnlitColor;
        public float4 _UnlitColorMap_ST;
        public float _UnlitLuminance;
    }

    public struct Varyings
    {
        [Position] public float4 position;
        [Location(0)] public float2 uv;
        [Location(1)] public float3 normalWS;
    }

    [Uniform(0)] public static MaterialData material;

    /// <summary>The material's map.</summary>
    public static class Maps
    {
        [Texture(0)] public static Texture2D<float4> _UnlitColorMap;
        [Sampler(0)] public static SamplerState sampler_UnlitColorMap;
    }

    static Varyings Transform(float3 position, float3 normal, float2 uv)
    {
        Varyings o;
        o.position = ShaderVariables.ObjectToClip(position);
        o.uv = uv * material._UnlitColorMap_ST.xy + material._UnlitColorMap_ST.zw;
        o.normalWS = ShaderVariables.ObjectToWorldNormal(normal);
        return o;
    }

    [Pass("PhysicalForward", LightMode = "PhysicalForward", CullProperty = "_Cull", ZWriteProperty = "_ZWrite")]
    [Blend(SourceProperty = "_SrcBlend", DestinationProperty = "_DstBlend")]
    public static class Forward
    {
        [VertexShader]
        public static Varyings Vertex([Location(VertexInputs.Position)] float3 position, [Location(VertexInputs.Normal)] float3 normal,
                                      [Location(VertexInputs.TexCoord0)] float2 uv) => Transform(position, normal, uv);

        [FragmentShader]
        [return: Target(0)]
        public static float4 Fragment(Varyings input)
        {
            float4 texel = Maps._UnlitColorMap.Sample(Maps.sampler_UnlitColorMap, input.uv);
            float3 color = Color.SRGBToLinear(material._UnlitColor.xyz) * texel.xyz * material._UnlitLuminance;
            return new float4(color * PhysicalGlobals.Exposure(), material._UnlitColor.w * texel.w);
        }
    }

    [Pass("PhysicalDepthNormals", LightMode = "PhysicalDepthNormals", ZWrite = true, CullProperty = "_Cull")]
    public static class DepthNormals
    {
        [VertexShader]
        public static Varyings Vertex([Location(VertexInputs.Position)] float3 position, [Location(VertexInputs.Normal)] float3 normal,
                                      [Location(VertexInputs.TexCoord0)] float2 uv) => Transform(position, normal, uv);

        [FragmentShader]
        [return: Target(0)]
        public static float4 Fragment(Varyings input) => new float4(Normalize(input.normalWS), 1.0f);
    }

    [Pass("ShadowCaster", LightMode = "ShadowCaster", ZWrite = true, ZTest = CompareFunction.LessEqual, ColorMask = ColorWriteMask.None, CullProperty = "_Cull")]
    public static class ShadowCasterPass
    {
        [VertexShader]
        public static Varyings Vertex([Location(VertexInputs.Position)] float3 position, [Location(VertexInputs.Normal)] float3 normal,
                                      [Location(VertexInputs.TexCoord0)] float2 uv)
        {
            Varyings o = Transform(position, normal, uv);
            o.position = ShadowCaster.ShadowPositionCS(position, normal);
            return o;
        }

        [FragmentShader]
        [return: Target(0)]
        public static float4 Fragment(Varyings input) => new float4(0.0f);
    }
}
