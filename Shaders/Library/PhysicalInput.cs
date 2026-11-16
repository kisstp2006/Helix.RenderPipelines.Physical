using HelixEngine.Builtin;
using HelixEngine.Shaders;
using static HelixEngine.Shaders.ShaderMath;

namespace HelixEngine.Rendering.Physical.Shaders;

/// <summary>The directional lights' vectors (four).</summary>
[System.Runtime.CompilerServices.InlineArray(PhysicalGlobals.MaxDirectionalLights)]
public struct DirectionalVectors
{
    private float4 element;
}

/// <summary>What the pipeline sets each camera (globals by name): the directional lights, the counts, the clusters'
/// grid, the camera's unjittered view and projection, the environment.</summary>
public struct PhysicalLightConstants
{
    /// <summary>xyz: the direction toward the light; w: the sun's angular radius (radians).</summary>
    public DirectionalVectors _DirectionalLightDirections;
    /// <summary>rgb: the linear color times the illuminance (lux).</summary>
    public DirectionalVectors _DirectionalLightColors;
    /// <summary>x: the directional lights, y: the point and spot lights, z: 1 when the first directional light has shadows.</summary>
    public float4 _PhysicalLightCounts;
    /// <summary>xyz: the clusters along x, y and the depth slices; w: the most lights of a cluster.</summary>
    public float4 _ClusterGrid;
    /// <summary>x: near, y: far, z: slices per natural log of the depth, w: minus the log of near times z.</summary>
    public float4 _ClusterParams;
    /// <summary>xy: the target's size, zw: one over it.</summary>
    public float4 _ClusterScreen;
    /// <summary>The camera's world to view matrix and its projection without the jitter.</summary>
    public float4x4 _ClusterView;
    public float4x4 _ClusterProjection;
    /// <summary>x: the luminance (nits) of a value of 1 of the environment; y: the ambient intensity; z: the reflection intensity.</summary>
    public float4 _PhysicalEnvironment;
}

/// <summary>A point or spot light (the pipeline's GpuPunctualLight).</summary>
public struct PunctualLight
{
    /// <summary>xyz: world position, w: range.</summary>
    public float4 positionRange;
    /// <summary>rgb: linear color times the intensity (candela), w: the shape's radius.</summary>
    public float4 colorRadius;
    /// <summary>xyz: the direction the light shines in, w: 1 for a spot light.</summary>
    public float4 directionType;
    /// <summary>x: the spot's scale, y: its offset; z: 1 when it lights the diffuse lobe, w: the specular lobe.</summary>
    public float4 spot;
}

/// <summary>The ambient probe in the shaders' form (RenderSettings.ambientProbe).</summary>
public struct PhysicalAmbientProbe
{
    public float4 unity_SHAr, unity_SHAg, unity_SHAb, unity_SHBr, unity_SHBg, unity_SHBb, unity_SHC;
}

/// <summary>
/// The pipeline's globals as every Physical shader reads them: uniform slot 2 (the lights) and 7 (the ambient probe),
/// with the core's cascaded shadows in slot 6 and the caster's constants in slot 1 (slot 0 is the material's); storage
/// slots 0 to 3 (the lights, the clusters' counts and indices, the exposure); texture slots 8 (the reflection), 9 (the
/// core's shadow atlas), 10 and 11 (the energy tables); 0 to 7 are the material's.
/// </summary>
public static class PhysicalGlobals
{
    public const int MaxDirectionalLights = 4;

    [Uniform(2)] public static PhysicalLightConstants lights;
    [Uniform(7)] public static PhysicalAmbientProbe ambient;

    [Storage(0)] public static StructuredBuffer<PunctualLight> _PunctualLights;
    [Storage(1)] public static StructuredBuffer<uint> _ClusterLightCounts;
    [Storage(2)] public static StructuredBuffer<uint> _ClusterLightIndices;
    /// <summary>x: the factor the frame is drawn with (pre-exposure), y: its EV100.</summary>
    [Storage(3)] public static StructuredBuffer<float4> _PhysicalExposure;

    /// <summary>The object's reflection probe cube (the scene's reflection without one).</summary>
    [Texture(8)] public static TextureCube<float4> unity_SpecCube0;
    [Sampler(8)] public static SamplerState samplerunity_SpecCube0;
    /// <summary>Per roughness (v) and cosine (u): the GGX lobe's A, B, C and the FON lobe's albedo.</summary>
    [Texture(10)] public static Texture2D<float4> _PhysicalEnergyTable;
    [Sampler(10)] public static SamplerState sampler_PhysicalEnergyTable;
    /// <summary>Per roughness (u): the GGX lobe's and the FON lobe's average albedo.</summary>
    [Texture(11)] public static Texture2D<float4> _PhysicalEnergyAverage;
    [Sampler(11)] public static SamplerState sampler_PhysicalEnergyAverage;

    /// <summary>The factor the frame is drawn with.</summary>
    public static float Exposure() => _PhysicalExposure[0].x;
}
