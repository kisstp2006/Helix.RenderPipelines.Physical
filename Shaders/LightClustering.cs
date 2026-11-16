using HelixEngine.Shaders;
using static HelixEngine.Shaders.ShaderMath;

namespace HelixEngine.Rendering.Physical.Shaders;

/// <summary>
/// Hidden/Physical/LightClustering: each cluster of the view (a screen tile between two logarithmic depths) lists the
/// point and spot lights whose range reaches its box in view space, up to the grid's most per cluster (the forward
/// passes read the list of their pixel's cluster). One thread per cluster, in groups of 4 x 4 x 4.
/// </summary>
[ComputeDefinition("Hidden/Physical/LightClustering")]
public sealed class LightClustering : ComputeDefinition
{
    [Uniform(2)] public static PhysicalLightConstants lights;
    [Storage(0)] public static StructuredBuffer<PunctualLight> _PunctualLights;
    [Storage(1)] public static RWStructuredBuffer<uint> _ClusterLightCounts;
    [Storage(2)] public static RWStructuredBuffer<uint> _ClusterLightIndices;

    /// <summary>A point of the view at a depth (positive, along the view) through a point of the screen (-1 to 1).</summary>
    static float3 ViewPoint(float2 ndc, float depth)
    {
        float4x4 p = lights._ClusterProjection;
        // x / -z = (ndc.x + P02) / P00 (the third column moves the centre, the lens shift).
        return new float3((ndc.x + p.c2.x) / p.c0.x * depth, (ndc.y + p.c2.y) / p.c1.y * depth, -depth);
    }

    [ComputeShader(4, 4, 4)]
    public static void AssignLights([DispatchThreadId] uint3 id)
    {
        float4 grid = lights._ClusterGrid;
        if (id.x >= (uint)grid.x || id.y >= (uint)grid.y || id.z >= (uint)grid.z)
            return;
        int cluster = ((int)id.z * (int)grid.y + (int)id.y) * (int)grid.x + (int)id.x;
        // The cluster's box: its tile's corners at its slice's near and far depth.
        float2 ndcMin = new float2(id.x / grid.x, id.y / grid.y) * 2.0f - new float2(1.0f);
        float2 ndcMax = new float2((id.x + 1u) / grid.x, (id.y + 1u) / grid.y) * 2.0f - new float2(1.0f);
        float4 p = lights._ClusterParams;
        float near = Exp(((float)id.z - p.w) / p.z), far = Exp(((float)id.z + 1.0f - p.w) / p.z);
        float3 a = ViewPoint(ndcMin, near), b = ViewPoint(ndcMax, near), c = ViewPoint(ndcMin, far), d = ViewPoint(ndcMax, far);
        float3 boxMin = Min(Min(a, b), Min(c, d)), boxMax = Max(Max(a, b), Max(c, d));
        int maxPerCluster = (int)grid.w;
        int count = 0;
        int lightCount = (int)lights._PhysicalLightCounts.y;
        for (int i = 0; i < lightCount; i++)
        {
            if (count >= maxPerCluster)
                break;
            PunctualLight light = _PunctualLights[i];
            float3 center = Mul(lights._ClusterView, new float4(light.positionRange.xyz, 1.0f)).xyz;
            float radius = light.positionRange.w;
            // The squared distance from the light to the box.
            float3 closest = Clamp(center, boxMin, boxMax);
            float3 delta = center - closest;
            if (Dot(delta, delta) <= radius * radius)
            {
                _ClusterLightIndices[cluster * maxPerCluster + count] = (uint)i;
                count++;
            }
        }
        _ClusterLightCounts[cluster] = (uint)count;
    }
}
