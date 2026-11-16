using HelixEngine.Builtin;
using HelixEngine.Rendering.ShaderLibrary;
using HelixEngine.Shaders;
using static HelixEngine.Shaders.ShaderMath;

namespace HelixEngine.Rendering.Physical.Shaders;

/// <summary>
/// The lighting of a shading point in physical units: the directional lights (lux; the first one with the core's
/// cascaded shadows, the sun's disc widening its highlights), the point and spot lights of the point's cluster
/// (candela, inverse square falloff, a window at the range), the environment (the ambient probe and the reflection in
/// the environment's luminance), the emission. The result is radiance; the caller multiplies it by the exposure.
/// </summary>
public static class PhysicalLighting
{
    const float SpecCubeLodSteps = 6.0f;

    /// <summary>The cluster of a world position: its screen tile under the unjittered projection, its depth slice.</summary>
    public static int ClusterIndex(float3 positionWS)
    {
        PhysicalLightConstants c = PhysicalGlobals.lights;
        float4 positionVS = Mul(c._ClusterView, new float4(positionWS, 1.0f));
        float4 clip = Mul(c._ClusterProjection, positionVS);
        float2 ndc = clip.xy / Max(Abs(clip.w), 1e-6f);
        int gx = (int)c._ClusterGrid.x, gy = (int)c._ClusterGrid.y, gz = (int)c._ClusterGrid.z;
        int x = Clamp((int)Floor((ndc.x * 0.5f + 0.5f) * c._ClusterGrid.x), 0, gx - 1);
        int y = Clamp((int)Floor((ndc.y * 0.5f + 0.5f) * c._ClusterGrid.y), 0, gy - 1);
        float depth = Max(-positionVS.z, c._ClusterParams.x);
        int z = Clamp((int)Floor(Log(depth) * c._ClusterParams.z + c._ClusterParams.w), 0, gz - 1);
        return (z * gy + y) * gx + x;
    }

    /// <summary>The ambient probe's irradiance over pi at a normal (L2 spherical harmonics).</summary>
    public static float3 SampleSH(float3 n)
    {
        PhysicalAmbientProbe a = PhysicalGlobals.ambient;
        float4 n1 = new float4(n, 1.0f);
        float3 l1 = new float3(Dot(a.unity_SHAr, n1), Dot(a.unity_SHAg, n1), Dot(a.unity_SHAb, n1));
        float4 vB = new float4(n.x * n.y, n.y * n.z, n.z * n.z, n.z * n.x);
        float3 l2 = new float3(Dot(a.unity_SHBr, vB), Dot(a.unity_SHBg, vB), Dot(a.unity_SHBb, vB));
        float vC = n.x * n.x - n.y * n.y;
        return Max(l1 + l2 + a.unity_SHC.xyz * vC, new float3(0.0f));
    }

    static float3 DecodeHDREnvironment(float4 encoded, float4 decodeInstructions)
    {
        float alpha = decodeInstructions.w * (encoded.w - 1.0f) + 1.0f;
        return decodeInstructions.x * Pow(Abs(alpha), decodeInstructions.y) * encoded.xyz;
    }

    /// <summary>The reflection probe's radiance along a direction, filtered for a roughness.</summary>
    public static float3 SampleReflection(float3 direction, float roughness)
    {
        float mip = roughness * (1.7f - 0.7f * roughness) * SpecCubeLodSteps;
        float4 encoded = PhysicalGlobals.unity_SpecCube0.SampleLevel(PhysicalGlobals.samplerunity_SpecCube0, direction, mip);
        return DecodeHDREnvironment(encoded, ShaderVariables.SpecCube0HDR());
    }

    /// <summary>A light's response added by lobe as the light's flags allow.</summary>
    static float3 Add(OpenPBRResponse r, float3 illuminance, float diffuse, float specular) => (r.diffuse * diffuse + r.specular * specular) * illuminance;

    /// <summary>The direct light of every light reaching the point.</summary>
    public static float3 DirectLighting(OpenPBRSurface s, float3 positionWS, float3 v)
    {
        PhysicalLightConstants c = PhysicalGlobals.lights;
        float3 color = new float3(0.0f);
        int directional = (int)c._PhysicalLightCounts.x;
        for (int i = 0; i < directional; i++)
        {
            float4 dir = c._DirectionalLightDirections[i];
            float3 lux = c._DirectionalLightColors[i].xyz;
            float shadow = i == 0 && c._PhysicalLightCounts.z > 0.5f ? CascadedShadows.MainLightRealtimeShadow(positionWS) : 1.0f;
            // The sun's disc widens the highlight: its angular radius added to the lobe's width.
            OpenPBRSurface sun = s;
            sun.specularRoughness = Sqrt(Min(1.0f, s.specularRoughness * s.specularRoughness + dir.w * 0.5f));
            color += Add(OpenPBR.Evaluate(sun, v, Normalize(dir.xyz)), lux * shadow, 1.0f, 1.0f);
        }
        if (c._PhysicalLightCounts.y < 0.5f)
            return color;
        int cluster = ClusterIndex(positionWS);
        int maxPerCluster = (int)c._ClusterGrid.w;
        int count = Min((int)PhysicalGlobals._ClusterLightCounts[cluster], maxPerCluster);
        for (int k = 0; k < count; k++)
        {
            PunctualLight light = PhysicalGlobals._PunctualLights[(int)PhysicalGlobals._ClusterLightIndices[cluster * maxPerCluster + k]];
            float3 toLight = light.positionRange.xyz - positionWS;
            float d2 = Dot(toLight, toLight);
            float d = Sqrt(d2);
            float3 l = toLight / Max(d, 1e-5f);
            float radius = light.colorRadius.w;
            float attenuation = 1.0f / Max(d2, Max(radius * radius, 1e-4f));
            // A smooth window to zero at the range (the light's culling range).
            float ratio = d / Max(light.positionRange.w, 1e-4f);
            float window = Saturate(1.0f - ratio * ratio * ratio * ratio);
            attenuation *= window * window;
            if (light.directionType.w > 0.5f)
            {
                float spot = Saturate(Dot(-l, light.directionType.xyz) * light.spot.x + light.spot.y);
                attenuation *= spot * spot;
            }
            if (attenuation <= 0.0f)
                continue;
            color += Add(OpenPBR.Evaluate(s, v, l), light.colorRadius.xyz * attenuation, light.spot.z, light.spot.w);
        }
        return color;
    }

    /// <summary>The environment's light: the ambient probe through the diffuse albedo, the reflection through the
    /// base's and the coat's specular albedos, in the environment's luminance.</summary>
    public static float3 EnvironmentLighting(OpenPBRSurface s, float3 v)
    {
        PhysicalLightConstants c = PhysicalGlobals.lights;
        OpenPBRAlbedo albedo = OpenPBR.Albedo(s, v);
        float3 n = s.normal;
        float3 r = Reflect(-v, n);
        float3 diffuse = albedo.diffuse * SampleSH(n) * c._PhysicalEnvironment.y;
        float3 specular = albedo.specular * SampleReflection(r, OpenPBR.EnvironmentRoughness(s)) * c._PhysicalEnvironment.z;
        if (s.coatWeight > 0.0f)
            specular += albedo.coat * SampleReflection(r, s.coatRoughness) * c._PhysicalEnvironment.z;
        return (diffuse + specular) * c._PhysicalEnvironment.x;
    }

    /// <summary>The radiance leaving the point toward the camera (not yet exposed).</summary>
    public static float3 Shade(OpenPBRSurface s, float3 positionWS)
    {
        float3 v = Normalize(ShaderVariables.camera.helix_WorldSpaceCameraPos.xyz - positionWS);
        // Normals facing away (back faces of double sided objects, interpolation) are turned toward the view.
        if (Dot(s.normal, v) < 0.0f)
            s.normal = Normalize(s.normal - v * (Dot(s.normal, v) * 1.01f));
        return DirectLighting(s, positionWS, v) + EnvironmentLighting(s, v) + s.emission;
    }
}
