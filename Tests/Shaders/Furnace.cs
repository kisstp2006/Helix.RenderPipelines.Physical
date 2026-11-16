using HelixEngine.Rendering.Physical.Shaders;
using HelixEngine.Shaders;
using static HelixEngine.Shaders.ShaderMath;

namespace Helix.RenderPipelines.Physical.TestShaders;

/// <summary>A surface and a view of the white furnace.</summary>
public struct FurnaceCase
{
    /// <summary>x: base weight, y: base diffuse roughness, z: base metalness, w: the view's cosine.</summary>
    public float4 baseParams;
    /// <summary>rgb: base color (linear), w: specular weight.</summary>
    public float4 baseColor;
    /// <summary>x: specular roughness, y: its anisotropy, z: specular IOR, w: coat weight.</summary>
    public float4 specularParams;
    /// <summary>rgb: specular color, w: coat roughness.</summary>
    public float4 specularColor;
    /// <summary>x: coat IOR, y: coat darkening.</summary>
    public float4 coatParams;
    /// <summary>rgb: coat color.</summary>
    public float4 coatColor;
}

/// <summary>
/// Tests/Physical/Furnace: per case the BSDF times the cosine integrated over the hemisphere by a grid of the cosine and
/// the azimuth (the albedo seen from the case's view under a uniform white environment), and the albedo the energy
/// tables give (OpenPBR.Albedo).
/// </summary>
[ComputeDefinition("Tests/Physical/Furnace")]
public sealed class Furnace : ComputeDefinition
{
    const int Cosines = 256;
    const int Azimuths = 512;

    public struct FurnaceData
    {
        /// <summary>x: the cases.</summary>
        public float4 _FurnaceCount;
    }

    [Uniform(0)] public static FurnaceData furnace;
    [Storage(4)] public static StructuredBuffer<FurnaceCase> _FurnaceCases;
    /// <summary>Per case: the integral (rgb), then the tables' albedo (rgb).</summary>
    [Storage(5)] public static RWStructuredBuffer<float4> _FurnaceResults;

    static OpenPBRSurface SurfaceOf(FurnaceCase c)
    {
        OpenPBRSurface s;
        s.baseWeight = c.baseParams.x;
        s.baseColor = c.baseColor.xyz;
        s.baseDiffuseRoughness = c.baseParams.y;
        s.baseMetalness = c.baseParams.z;
        s.specularWeight = c.baseColor.w;
        s.specularColor = c.specularColor.xyz;
        s.specularRoughness = c.specularParams.x;
        s.specularRoughnessAnisotropy = c.specularParams.y;
        s.specularIor = c.specularParams.z;
        s.coatWeight = c.specularParams.w;
        s.coatColor = c.coatColor.xyz;
        s.coatRoughness = c.specularColor.w;
        s.coatIor = c.coatParams.x;
        s.coatDarkening = c.coatParams.y;
        s.emission = new float3(0.0f);
        s.opacity = 1.0f;
        s.normal = new float3(0.0f, 0.0f, 1.0f);
        s.tangent = new float3(1.0f, 0.0f, 0.0f);
        s.bitangent = new float3(0.0f, 1.0f, 0.0f);
        return s;
    }

    [ComputeShader(64)]
    public static void Integrate([DispatchThreadId] uint3 id)
    {
        if (id.x >= (uint)furnace._FurnaceCount.x)
            return;
        FurnaceCase c = _FurnaceCases[(int)id.x];
        OpenPBRSurface s = SurfaceOf(c);
        float muV = c.baseParams.w;
        float3 v = new float3(Sqrt(Max(1.0f - muV * muV, 0.0f)), 0.0f, muV);
        float3 sum = new float3(0.0f);
        for (int i = 0; i < Cosines; i++)
        {
            float mu = ((float)i + 0.5f) / (float)Cosines;
            float sinTheta = Sqrt(Max(1.0f - mu * mu, 0.0f));
            for (int j = 0; j < Azimuths; j++)
            {
                float phi = 6.28318530718f * ((float)j + 0.5f) / (float)Azimuths;
                float3 l = new float3(sinTheta * Cos(phi), sinTheta * Sin(phi), mu);
                OpenPBRResponse r = OpenPBR.Evaluate(s, v, l);
                sum += r.diffuse + r.specular;
            }
        }
        // d omega = d mu d phi.
        sum *= 6.28318530718f / ((float)Cosines * (float)Azimuths);
        OpenPBRAlbedo a = OpenPBR.Albedo(s, v);
        _FurnaceResults[(int)id.x * 2] = new float4(sum, 1.0f);
        _FurnaceResults[(int)id.x * 2 + 1] = new float4(a.diffuse + a.specular + a.coat, 1.0f);
    }
}
