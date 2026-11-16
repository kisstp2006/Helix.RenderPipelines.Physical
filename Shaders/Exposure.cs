using HelixEngine.Rendering.ShaderLibrary;
using HelixEngine.Shaders;
using static HelixEngine.Shaders.ShaderMath;

namespace HelixEngine.Rendering.Physical.Shaders;

/// <summary>
/// Hidden/Physical/Exposure: the automatic exposure. ClearHistogram empties the bins; Histogram counts each pixel of
/// the pre-exposed frame in the bin of its EV100 (its luminance over the frame's exposure, between the limits); Adapt
/// averages the bins between the low and the high percentage, clamps the result to the limits, takes the compensation
/// off and moves the exposure toward it (at once on a cut).
/// </summary>
[ComputeDefinition("Hidden/Physical/Exposure")]
public sealed class AutomaticExposure : ComputeDefinition
{
    public const int Bins = 128;

    public struct ExposureData
    {
        /// <summary>x: the lowest EV100, y: the highest, z: the low share, w: the high share.</summary>
        public float4 _ExposureParams;
        /// <summary>x: the compensation (EV), y: EV per second toward a brighter scene, z: toward a darker one, w: the
        /// frame's time (negative: at once).</summary>
        public float4 _ExposureSpeeds;
        /// <summary>xy: the frame's size.</summary>
        public float4 _ExposureSourceSize;
    }

    [Uniform(0)] public static ExposureData exposure;
    [Storage(0)] public static RWStructuredBuffer<uint> _ExposureHistogram;
    /// <summary>x: the factor the frame is drawn with, y: its EV100, z: the target EV100.</summary>
    [Storage(1)] public static RWStructuredBuffer<float4> _PhysicalExposure;
    [Texture(0)] public static Texture2D<float4> _ExposureSource;

    [ComputeShader(64)]
    public static void ClearHistogram([DispatchThreadId] uint3 id)
    {
        if (id.x < (uint)Bins)
            _ExposureHistogram[(int)id.x] = 0u;
    }

    [ComputeShader(8, 8)]
    public static void Histogram([DispatchThreadId] uint3 id)
    {
        if (id.x >= (uint)exposure._ExposureSourceSize.x || id.y >= (uint)exposure._ExposureSourceSize.y)
            return;
        float3 c = _ExposureSource.Load(new int2((int)id.x, (int)id.y), 0).xyz;
        float luminance = Color.Luminance(c) / Max(_PhysicalExposure[0].x, 1e-20f);
        if (luminance <= 1e-6f)
            return;
        // EV100 of a luminance: log2(L * 100 / 12.5).
        float ev = Log2(luminance * 8.0f);
        float t = Saturate((ev - exposure._ExposureParams.x) / (exposure._ExposureParams.y - exposure._ExposureParams.x));
        int bin = Min((int)(t * (float)Bins), Bins - 1);
        InterlockedAdd(_ExposureHistogram, bin, 1u);
    }

    [ComputeShader(1)]
    public static void Adapt([DispatchThreadId] uint3 id)
    {
        float total = 0.0f;
        for (int i = 0; i < Bins; i++)
            total += (float)_ExposureHistogram[i];
        float4 previous = _PhysicalExposure[0];
        float low = total * exposure._ExposureParams.z, high = total * exposure._ExposureParams.w;
        float range = exposure._ExposureParams.y - exposure._ExposureParams.x;
        // The bins' counts between the two shares, each at its bin's middle.
        float sum = 0.0f, weight = 0.0f, below = 0.0f;
        for (int i = 0; i < Bins; i++)
        {
            float count = (float)_ExposureHistogram[i];
            float from = Max(below, low), to = Min(below + count, high);
            float w = Max(to - from, 0.0f);
            sum += w * (exposure._ExposureParams.x + ((float)i + 0.5f) / (float)Bins * range);
            weight += w;
            below += count;
        }
        float measured = weight > 0.0f ? sum / weight : previous.y + exposure._ExposureSpeeds.x;
        float target = Clamp(measured, exposure._ExposureParams.x, exposure._ExposureParams.y) - exposure._ExposureSpeeds.x;
        float dt = exposure._ExposureSpeeds.w;
        float ev = target;
        if (dt >= 0.0f)
        {
            float speed = target > previous.y ? exposure._ExposureSpeeds.y : exposure._ExposureSpeeds.z;
            ev = previous.y + (target - previous.y) * (1.0f - Exp(-dt * speed));
        }
        _PhysicalExposure[0] = new float4(1.0f / (1.2f * Exp2(ev)), ev, target, 0.0f);
    }
}
