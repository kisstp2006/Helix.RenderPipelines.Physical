using HelixEngine.Builtin;
using HelixEngine.Rendering.ShaderLibrary;
using HelixEngine.Shaders;
using static HelixEngine.Shaders.ShaderMath;

namespace HelixEngine.Rendering.Physical.Shaders;

/// <summary>
/// Physical/OpenPBR Lite: OpenPBR Surface's base (weight, color, diffuse roughness, metalness), specular (weight,
/// color, roughness, anisotropy, IOR) and coat (weight, color, roughness, IOR, darkening), emission in nits, opacity,
/// with a base color map, a roughness and metalness map (green and blue, as glTF's), a normal map and an emission map.
/// Colors are sRGB. Passes: PhysicalForward, PhysicalDepthNormals, ShadowCaster. With the global keyword
/// _PHYSICAL_BINDLESS the maps are read through the device's bindless tables (their indices in the material's block).
/// </summary>
[Shader("Physical/OpenPBR Lite")]
[Tag("RenderType", "Opaque")]
[Tag("RenderPipeline", "PhysicalPipeline")]
public sealed class OpenPBRLite : ShaderDefinition
{
    [Property("Base Weight"), Range(0.0f, 1.0f), Default(1.0f)] public float _BaseWeight;
    [Property("Base Color"), MainColor, Default(0.8f, 0.8f, 0.8f, 1.0f)] public float4 _BaseColor;
    [Property("Base Color Map"), MainTexture, DefaultTexture("white")] public Texture2D<float4> _BaseColorMap;
    [Property("Base Diffuse Roughness"), Range(0.0f, 1.0f), Default(0.0f)] public float _BaseDiffuseRoughness;
    [Property("Base Metalness"), Range(0.0f, 1.0f), Default(0.0f)] public float _BaseMetalness;
    [Property("Specular Weight"), Range(0.0f, 1.0f), Default(1.0f)] public float _SpecularWeight;
    [Property("Specular Color"), Default(1.0f, 1.0f, 1.0f, 1.0f)] public float4 _SpecularColor;
    [Property("Specular Roughness"), Range(0.0f, 1.0f), Default(0.3f)] public float _SpecularRoughness;
    [Property("Specular Roughness Anisotropy"), Range(0.0f, 1.0f), Default(0.0f)] public float _SpecularRoughnessAnisotropy;
    [Property("Specular IOR"), Range(1.0f, 3.0f), Default(1.5f)] public float _SpecularIOR;
    [Property("Roughness Metalness Map"), NoScaleOffset, DefaultTexture("white")] public Texture2D<float4> _RoughnessMetalnessMap;
    [Property("Coat Weight"), Range(0.0f, 1.0f), Default(0.0f)] public float _CoatWeight;
    [Property("Coat Color"), Default(1.0f, 1.0f, 1.0f, 1.0f)] public float4 _CoatColor;
    [Property("Coat Roughness"), Range(0.0f, 1.0f), Default(0.0f)] public float _CoatRoughness;
    [Property("Coat IOR"), Range(1.0f, 3.0f), Default(1.6f)] public float _CoatIOR;
    [Property("Coat Darkening"), Range(0.0f, 1.0f), Default(1.0f)] public float _CoatDarkening;
    [Property("Emission Luminance (nits)"), Default(0.0f)] public float _EmissionLuminance;
    [Property("Emission Color"), Default(1.0f, 1.0f, 1.0f, 1.0f)] public float4 _EmissionColor;
    [Property("Emission Map"), NoScaleOffset, DefaultTexture("white")] public Texture2D<float4> _EmissionMap;
    [Property("Normal Map"), NoScaleOffset, DefaultTexture("bump")] public Texture2D<float4> _NormalMap;
    [Property("Normal Scale"), Default(1.0f)] public float _NormalScale;
    [Property("Opacity"), Range(0.0f, 1.0f), Default(1.0f)] public float _Opacity;
    [Property("Alpha Cutoff"), Range(0.0f, 1.0f), Default(0.5f)] public float _Cutoff;
    // Surface options (the passes' states come from them).
    [Property("Surface Type"), Default(0.0f)] public float _Surface;
    [Property("Cull Mode"), Default(2.0f)] public float _Cull;
    [Property("Src Blend"), Default(1.0f)] public float _SrcBlend;
    [Property("Dst Blend"), Default(0.0f)] public float _DstBlend;
    [Property("ZWrite"), Default(1.0f)] public float _ZWrite;

    [Keyword(KeywordScope.Local, ShaderFeature = true)] public static readonly bool _NORMALMAP;
    [Keyword(KeywordScope.Local, ShaderFeature = true)] public static readonly bool _EMISSION;
    [Keyword(KeywordScope.Local, ShaderFeature = true)] public static readonly bool _ALPHATEST_ON;
    [Keyword(KeywordScope.Local, ShaderFeature = true)] public static readonly bool _SURFACE_TYPE_TRANSPARENT;
    [Keyword(KeywordScope.Global)] public static readonly bool _PHYSICAL_BINDLESS;

    public struct MaterialData
    {
        public float4 _BaseColor;
        public float4 _SpecularColor;
        public float4 _CoatColor;
        public float4 _EmissionColor;
        public float4 _BaseColorMap_ST;
        public float _BaseWeight;
        public float _BaseDiffuseRoughness;
        public float _BaseMetalness;
        public float _SpecularWeight;
        public float _SpecularRoughness;
        public float _SpecularRoughnessAnisotropy;
        public float _SpecularIOR;
        public float _CoatWeight;
        public float _CoatRoughness;
        public float _CoatIOR;
        public float _CoatDarkening;
        public float _EmissionLuminance;
        public float _NormalScale;
        public float _Opacity;
        public float _Cutoff;
        public float _Surface;
        // The maps' bindless indices (the renderer writes them for textures it registered).
        public uint _BaseColorMap_Bindless;
        public uint _RoughnessMetalnessMap_Bindless;
        public uint _NormalMap_Bindless;
        public uint _EmissionMap_Bindless;
    }

    public struct Varyings
    {
        [Position] public float4 position;
        [Location(0)] public float2 uv;
        [Location(1)] public float3 positionWS;
        [Location(2)] public float3 normalWS;
        [Location(3)] public float4 tangentWS;
    }

    public struct DepthVaryings
    {
        [Position] public float4 position;
        [Location(0)] public float2 uv;
    }

    [Uniform(0)] public static MaterialData material;

    /// <summary>The material's maps as the passes bind them.</summary>
    public static class Maps
    {
        [Texture(0)] public static Texture2D<float4> _BaseColorMap;
        [Sampler(0)] public static SamplerState sampler_BaseColorMap;
        [Texture(1)] public static Texture2D<float4> _RoughnessMetalnessMap;
        [Sampler(1)] public static SamplerState sampler_RoughnessMetalnessMap;
        [Texture(2)] public static Texture2D<float4> _NormalMap;
        [Sampler(2)] public static SamplerState sampler_NormalMap;
        [Texture(3)] public static Texture2D<float4> _EmissionMap;
        [Sampler(3)] public static SamplerState sampler_EmissionMap;
    }

    static float2 BaseUV(float2 uv) => uv * material._BaseColorMap_ST.xy + material._BaseColorMap_ST.zw;

    // The same texel through a binding or through the bindless table.
    static float4 BaseColorMap(float2 uv) =>
        _PHYSICAL_BINDLESS ? new BindlessTexture(material._BaseColorMap_Bindless).Sample(uv) : Maps._BaseColorMap.Sample(Maps.sampler_BaseColorMap, uv);

    static float4 RoughnessMetalnessMap(float2 uv) =>
        _PHYSICAL_BINDLESS ? new BindlessTexture(material._RoughnessMetalnessMap_Bindless).Sample(uv) : Maps._RoughnessMetalnessMap.Sample(Maps.sampler_RoughnessMetalnessMap, uv);

    static float4 NormalMap(float2 uv) =>
        _PHYSICAL_BINDLESS ? new BindlessTexture(material._NormalMap_Bindless).Sample(uv) : Maps._NormalMap.Sample(Maps.sampler_NormalMap, uv);

    static float4 EmissionMap(float2 uv) =>
        _PHYSICAL_BINDLESS ? new BindlessTexture(material._EmissionMap_Bindless).Sample(uv) : Maps._EmissionMap.Sample(Maps.sampler_EmissionMap, uv);

    /// <summary>The opacity of a pixel (clipped below the cutoff with alpha testing).</summary>
    static float Opacity(float baseAlpha)
    {
        float alpha = baseAlpha * material._BaseColor.w * material._Opacity;
        if (_ALPHATEST_ON && alpha < material._Cutoff)
            Discard();
        return _SURFACE_TYPE_TRANSPARENT ? alpha : 1.0f;
    }

    /// <summary>The surface of a pixel.</summary>
    static OpenPBRSurface Surface(Varyings input)
    {
        float2 uv = input.uv;
        float4 baseTexel = BaseColorMap(uv);
        float4 rm = RoughnessMetalnessMap(uv);
        OpenPBRSurface s;
        s.baseWeight = material._BaseWeight;
        s.baseColor = Color.SRGBToLinear(material._BaseColor.xyz) * baseTexel.xyz;
        s.baseDiffuseRoughness = material._BaseDiffuseRoughness;
        s.baseMetalness = material._BaseMetalness * rm.z;
        s.specularWeight = material._SpecularWeight;
        s.specularColor = Color.SRGBToLinear(material._SpecularColor.xyz);
        s.specularRoughness = Saturate(material._SpecularRoughness * rm.y);
        s.specularRoughnessAnisotropy = material._SpecularRoughnessAnisotropy;
        s.specularIor = material._SpecularIOR;
        s.coatWeight = material._CoatWeight;
        s.coatColor = Color.SRGBToLinear(material._CoatColor.xyz);
        s.coatRoughness = material._CoatRoughness;
        s.coatIor = material._CoatIOR;
        s.coatDarkening = material._CoatDarkening;
        s.emission = _EMISSION ? Color.SRGBToLinear(material._EmissionColor.xyz) * EmissionMap(uv).xyz * material._EmissionLuminance : new float3(0.0f);
        s.opacity = Opacity(baseTexel.w);
        float3 n = Normalize(input.normalWS);
        float3 t = Normalize(input.tangentWS.xyz - n * Dot(input.tangentWS.xyz, n));
        float3 b = Cross(n, t) * (input.tangentWS.w < 0.0f ? -1.0f : 1.0f);
        if (_NORMALMAP)
        {
            float3 ts = Packing.UnpackNormalScale(NormalMap(uv), material._NormalScale);
            n = Normalize(t * ts.x + b * ts.y + n * ts.z);
            t = Normalize(t - n * Dot(t, n));
            b = Cross(n, t) * (input.tangentWS.w < 0.0f ? -1.0f : 1.0f);
        }
        s.normal = n;
        s.tangent = t;
        s.bitangent = b;
        return s;
    }

    static Varyings Transform(float3 position, float3 normal, float4 tangent, float2 uv)
    {
        Varyings o;
        o.position = ShaderVariables.ObjectToClip(position);
        o.uv = BaseUV(uv);
        o.positionWS = ShaderVariables.ObjectToWorld(position);
        o.normalWS = ShaderVariables.ObjectToWorldNormal(normal);
        o.tangentWS = new float4(SpaceTransforms.TransformObjectToWorldDir(tangent.xyz), tangent.w);
        return o;
    }

    [Pass("PhysicalForward", LightMode = "PhysicalForward", CullProperty = "_Cull", ZWriteProperty = "_ZWrite")]
    [Blend(SourceProperty = "_SrcBlend", DestinationProperty = "_DstBlend")]
    public static class Forward
    {
        [VertexShader]
        public static Varyings Vertex([Location(VertexInputs.Position)] float3 position, [Location(VertexInputs.Normal)] float3 normal,
                                      [Location(VertexInputs.Tangent)] float4 tangent, [Location(VertexInputs.TexCoord0)] float2 uv) =>
            Transform(position, normal, tangent, uv);

        [FragmentShader]
        [return: Target(0)]
        public static float4 Fragment(Varyings input)
        {
            OpenPBRSurface s = Surface(input);
            float3 radiance = PhysicalLighting.Shade(s, input.positionWS);
            return new float4(radiance * PhysicalGlobals.Exposure(), s.opacity);
        }
    }

    [Pass("PhysicalDepthNormals", LightMode = "PhysicalDepthNormals", ZWrite = true, CullProperty = "_Cull")]
    public static class DepthNormals
    {
        [VertexShader]
        public static Varyings Vertex([Location(VertexInputs.Position)] float3 position, [Location(VertexInputs.Normal)] float3 normal,
                                      [Location(VertexInputs.Tangent)] float4 tangent, [Location(VertexInputs.TexCoord0)] float2 uv) =>
            Transform(position, normal, tangent, uv);

        /// <summary>xyz: the world normal, w: the specular roughness.</summary>
        [FragmentShader]
        [return: Target(0)]
        public static float4 Fragment(Varyings input)
        {
            OpenPBRSurface s = Surface(input);
            return new float4(s.normal, s.specularRoughness);
        }
    }

    [Pass("ShadowCaster", LightMode = "ShadowCaster", ZWrite = true, ZTest = CompareFunction.LessEqual, ColorMask = ColorWriteMask.None, CullProperty = "_Cull")]
    public static class ShadowCasterPass
    {
        [VertexShader]
        public static DepthVaryings Vertex([Location(VertexInputs.Position)] float3 position, [Location(VertexInputs.Normal)] float3 normal,
                                           [Location(VertexInputs.TexCoord0)] float2 uv)
        {
            DepthVaryings o;
            o.position = ShadowCaster.ShadowPositionCS(position, normal);
            o.uv = BaseUV(uv);
            return o;
        }

        [FragmentShader]
        [return: Target(0)]
        public static float4 Fragment(DepthVaryings input)
        {
            Opacity(BaseColorMap(input.uv).w);
            return new float4(0.0f);
        }
    }
}
