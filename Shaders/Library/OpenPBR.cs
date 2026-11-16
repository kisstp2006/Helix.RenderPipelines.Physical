using HelixEngine.Rendering.ShaderLibrary;
using HelixEngine.Shaders;
using static HelixEngine.Shaders.ShaderMath;

namespace HelixEngine.Rendering.Physical.Shaders;

/// <summary>The OpenPBR Surface parameters of a shading point (the base, specular and coat layers, emission, geometry).</summary>
public struct OpenPBRSurface
{
    public float baseWeight;
    /// <summary>Linear.</summary>
    public float3 baseColor;
    public float baseDiffuseRoughness;
    public float baseMetalness;
    public float specularWeight;
    /// <summary>Linear: the dielectric's tint, the metal's F82 tint.</summary>
    public float3 specularColor;
    public float specularRoughness;
    public float specularRoughnessAnisotropy;
    public float specularIor;
    public float coatWeight;
    /// <summary>Linear: the coat's transmission tint.</summary>
    public float3 coatColor;
    public float coatRoughness;
    public float coatIor;
    public float coatDarkening;
    /// <summary>Linear luminance in nits.</summary>
    public float3 emission;
    public float opacity;
    /// <summary>The shading frame (world space): normal, tangent and bitangent.</summary>
    public float3 normal;
    public float3 tangent;
    public float3 bitangent;
}

/// <summary>A light's response split by lobe (light affecting only one of them).</summary>
public struct OpenPBRResponse
{
    public float3 diffuse;
    public float3 specular;
}

/// <summary>The directional albedos of the lobes: the diffuse part, the base's specular, the coat.</summary>
public struct OpenPBRAlbedo
{
    public float3 diffuse;
    public float3 specular;
    public float3 coat;
}

/// <summary>
/// OpenPBR Surface (base, specular and coat) from the public specification: the base's EON rough diffuse (the Fujii
/// Oren-Nayar lobe and its energy compensation lobe) and its metal with the F82 tint Fresnel, the specular GGX lobe with
/// height correlated Smith masking, anisotropy and multiple scattering compensation, the dielectric's specular over the
/// diffuse by albedo scaling, the coat over the base with its absorption tint, the internal reflection darkening and the
/// base's roughening. The albedos come from the pipeline's energy tables (Tools/EnergyTables). Evaluate gives the BSDF
/// times the cosine; Albedo the directional albedo (the response to a uniform environment of radiance 1).
/// </summary>
public static class OpenPBR
{
    const float TableSize = 32.0f;
    const float MinRoughness = 0.02f;
    const float InvPi = 0.31830988618f;

    // ------------------------------------------------------------------------------------------------ tables

    static float2 TableUV(float x, float y) =>
        new float2((Saturate(x) * (TableSize - 1.0f) + 0.5f) / TableSize, (Saturate(y) * (TableSize - 1.0f) + 0.5f) / TableSize);

    /// <summary>GGX's A, B, C and the FON lobe's albedo at a cosine and a roughness.</summary>
    public static float4 Table(float mu, float roughness) =>
        PhysicalGlobals._PhysicalEnergyTable.SampleLevel(PhysicalGlobals.sampler_PhysicalEnergyTable, TableUV(mu, roughness), 0.0f);

    /// <summary>x: GGX's average albedo, y: the FON lobe's, at a roughness.</summary>
    public static float2 Average(float roughness) =>
        PhysicalGlobals._PhysicalEnergyAverage.SampleLevel(PhysicalGlobals.sampler_PhysicalEnergyAverage, new float2(TableUV(roughness, 0.0f).x, 0.5f), 0.0f).xy;

    // ------------------------------------------------------------------------------------------------ microfacets

    /// <summary>The GGX distribution (isotropic).</summary>
    public static float D_GGX(float NoH, float alpha)
    {
        float a2 = alpha * alpha;
        float d = NoH * NoH * (a2 - 1.0f) + 1.0f;
        return a2 / (Common.PI * d * d);
    }

    /// <summary>The anisotropic GGX distribution with the roughness along the tangent and the bitangent.</summary>
    public static float D_GGX_Aniso(float NoH, float ToH, float BoH, float at, float ab)
    {
        float3 v = new float3(ab * ToH, at * BoH, at * ab * NoH);
        float v2 = Dot(v, v);
        float w2 = at * ab / v2;
        return at * ab * w2 * w2 * InvPi;
    }

    /// <summary>Height correlated Smith G2 over 4 NoV NoL.</summary>
    public static float V_SmithHC(float NoV, float NoL, float alpha)
    {
        float a2 = alpha * alpha;
        float lv = NoL * Sqrt(NoV * NoV * (1.0f - a2) + a2);
        float ll = NoV * Sqrt(NoL * NoL * (1.0f - a2) + a2);
        return 0.5f / Max(lv + ll, 1e-7f);
    }

    public static float V_SmithHC_Aniso(float NoV, float NoL, float ToV, float BoV, float ToL, float BoL, float at, float ab)
    {
        float lv = NoL * Length(new float3(at * ToV, ab * BoV, NoV));
        float ll = NoV * Length(new float3(at * ToL, ab * BoL, NoL));
        return 0.5f / Max(lv + ll, 1e-7f);
    }

    public static float3 FresnelSchlick(float3 f0, float VoH)
    {
        float f = Pow(1.0f - Saturate(VoH), 5.0f);
        return f0 + (new float3(1.0f) - f0) * f;
    }

    /// <summary>The F82 tint's factor: F = Schlick(F0) - mu (1 - mu)^6 * k, with F(1/7) the tint times Schlick's.</summary>
    public static float3 F82Factor(float3 f0, float3 tint)
    {
        const float muBar = 1.0f / 7.0f;
        float denom = muBar * Pow(1.0f - muBar, 6.0f);
        float3 schlickBar = f0 + (new float3(1.0f) - f0) * Pow(1.0f - muBar, 5.0f);
        return schlickBar * (new float3(1.0f) - tint) / denom;
    }

    public static float3 FresnelF82(float3 f0, float3 k, float VoH)
    {
        float mu = Saturate(VoH);
        return Max(FresnelSchlick(f0, mu) - k * (mu * Pow(1.0f - mu, 6.0f)), new float3(0.0f));
    }

    /// <summary>F0 of a dielectric of an IOR (in air).</summary>
    public static float IorToF0(float ior)
    {
        float r = (ior - 1.0f) / (ior + 1.0f);
        return r * r;
    }

    /// <summary>The multiple scattering compensation of a GGX lobe: its single scattering times 1 + F0 (1 - E) / E.</summary>
    public static float3 MultipleScattering(float3 f0, float albedoOne) => new float3(1.0f) + f0 * ((1.0f - albedoOne) / Max(albedoOne, 1e-4f));

    // ------------------------------------------------------------------------------------------------ the layers

    /// <summary>The effective roughness of the base under the coat (roughened by it).</summary>
    public static float BaseRoughness(OpenPBRSurface s)
    {
        float r = s.specularRoughness, rc = s.coatRoughness;
        float under = Min(1.0f, Sqrt(Sqrt(r * r * r * r + 2.0f * rc * rc * rc * rc)));
        return Lerp(r, under, s.coatWeight);
    }

    /// <summary>The coat's internal reflection darkening of an albedo: rho (1 - K) / (1 - rho K).</summary>
    public static float3 Darken(float3 rho, OpenPBRSurface s)
    {
        float2 avg = Average(Max(s.coatRoughness, MinRoughness));
        float f0 = IorToF0(s.coatIor);
        float eAvg = Saturate((f0 + (1.0f - f0) * 0.05f) * avg.x);
        float k = 1.0f - (1.0f - eAvg) / Max(s.coatIor * s.coatIor, 1.0f);
        k = Saturate(k) * s.coatDarkening * s.coatWeight;
        return rho * (1.0f - k) / Max(new float3(1.0f) - rho * k, new float3(1e-4f));
    }

    /// <summary>A GGX lobe's directional albedo with Schlick's Fresnel of F0 (and its multiple scattering), at a cosine.</summary>
    public static float3 DielectricAlbedo(float3 f0, float mu, float roughness)
    {
        float4 t = Table(mu, roughness);
        float one = t.x + t.y;
        return (f0 * t.x + new float3(t.y)) * MultipleScattering(f0, one);
    }

    /// <summary>The metal lobe's directional albedo with the F82 tint.</summary>
    public static float3 MetalAlbedo(float3 f0, float3 k, float mu, float roughness)
    {
        float4 t = Table(mu, roughness);
        float one = t.x + t.y;
        return Max(f0 * t.x + new float3(t.y) - k * t.z, new float3(0.0f)) * MultipleScattering(f0, one);
    }

    /// <summary>The EON diffuse lobe's directional albedo of an albedo rho at a cosine.</summary>
    public static float3 DiffuseAlbedo(float3 rho, float mu, float sigma)
    {
        float e = Table(mu, sigma).w;
        float eAvg = Average(sigma).y;
        float3 rhoMs = rho * rho * eAvg / Max(new float3(1.0f) - rho * (1.0f - eAvg), new float3(1e-4f));
        return rho * e + rhoMs * (1.0f - e);
    }

    // ------------------------------------------------------------------------------------------------ the parts

    struct Parts
    {
        public float3 diffuseRho;
        public float3 dielectricF0;
        public float3 dielectricTint;
        public float3 metalF0;
        public float3 metalK;
        public float coatF0;
        public float baseRoughness;
    }

    static Parts PartsOf(OpenPBRSurface s)
    {
        Parts p;
        float3 baseColor = Darken(s.baseColor * s.baseWeight, s);
        p.diffuseRho = baseColor;
        p.dielectricF0 = new float3(Min(IorToF0(s.specularIor) * s.specularWeight, 1.0f));
        p.dielectricTint = s.specularColor;
        p.metalF0 = baseColor * s.specularWeight;
        p.metalK = F82Factor(p.metalF0, s.specularColor);
        p.coatF0 = IorToF0(s.coatIor);
        p.baseRoughness = Max(BaseRoughness(s), MinRoughness);
        return p;
    }

    /// <summary>The coat's albedo at a cosine (zero without a coat).</summary>
    static float3 CoatAlbedo(OpenPBRSurface s, Parts p, float mu) =>
        s.coatWeight > 0.0f ? DielectricAlbedo(new float3(p.coatF0), mu, Max(s.coatRoughness, MinRoughness)) * s.coatWeight : new float3(0.0f);

    /// <summary>What reaches the base through the coat seen at a cosine: the share the coat does not reflect, tinted.</summary>
    static float3 BaseThroughCoat(OpenPBRSurface s, Parts p, float mu)
    {
        if (s.coatWeight <= 0.0f)
            return new float3(1.0f);
        float3 coat = DielectricAlbedo(new float3(p.coatF0), mu, Max(s.coatRoughness, MinRoughness));
        return new float3(1.0f - s.coatWeight) + s.coatWeight * (new float3(1.0f) - coat) * s.coatColor;
    }

    /// <summary>A GGX lobe (D V) along a half vector, with anisotropy when the roughnesses differ.</summary>
    static float SpecularDV(OpenPBRSurface s, float roughness, float3 v, float3 l, float3 h, float NoV, float NoL, float NoH, bool anisotropic)
    {
        float alpha = roughness * roughness;
        if (!anisotropic || s.specularRoughnessAnisotropy <= 0.0f)
            return D_GGX(NoH, alpha) * V_SmithHC(NoV, NoL, alpha);
        float a = s.specularRoughnessAnisotropy;
        float at = Max(alpha * Sqrt(2.0f / (1.0f + (1.0f - a) * (1.0f - a))), 1e-4f);
        float ab = Max((1.0f - a) * at, 1e-4f);
        float ToH = Dot(s.tangent, h), BoH = Dot(s.bitangent, h);
        float ToV = Dot(s.tangent, v), BoV = Dot(s.bitangent, v), ToL = Dot(s.tangent, l), BoL = Dot(s.bitangent, l);
        return D_GGX_Aniso(NoH, ToH, BoH, at, ab) * V_SmithHC_Aniso(NoV, NoL, ToV, BoV, ToL, BoL, at, ab);
    }

    /// <summary>The FON lobe of roughness sigma (over rho / pi).</summary>
    static float FonLobe(float NoV, float NoL, float VoL, float sigma)
    {
        const float c1 = 0.5f - 2.0f / (3.0f * Common.PI);
        float a = 1.0f / (1.0f + c1 * sigma);
        float b = sigma * a;
        float sterm = VoL - NoV * NoL;
        float t = sterm > 0.0f ? Max(NoV, NoL) : 1.0f;
        return a + b * sterm / t;
    }

    /// <summary>The BSDF of the surface times the cosine for a light from l seen from v (both toward the surface's outside).</summary>
    public static OpenPBRResponse Evaluate(OpenPBRSurface s, float3 v, float3 l)
    {
        OpenPBRResponse r;
        r.diffuse = new float3(0.0f);
        r.specular = new float3(0.0f);
        float3 n = s.normal;
        float NoL = Dot(n, l);
        float NoV = Max(Dot(n, v), 1e-4f);
        if (NoL <= 0.0f)
            return r;
        float3 h = Normalize(v + l);
        float NoH = Saturate(Dot(n, h)), VoH = Saturate(Dot(v, h)), VoL = Dot(v, l);
        Parts p = PartsOf(s);

        // The base: metal, or the dielectric's specular over its diffuse.
        float4 tv = Table(NoV, p.baseRoughness);
        float one = tv.x + tv.y;
        float dv = SpecularDV(s, p.baseRoughness, v, l, h, NoV, NoL, NoH, true);
        float3 metal = FresnelF82(p.metalF0, p.metalK, VoH) * dv * MultipleScattering(p.metalF0, one);
        float3 dielectricF = p.dielectricTint * FresnelSchlick(p.dielectricF0, VoH);
        float3 dielectric = dielectricF * dv * MultipleScattering(p.dielectricF0, one);
        float3 dielectricAlbedo = p.dielectricTint * DielectricAlbedo(p.dielectricF0, NoV, p.baseRoughness);
        float sigma = s.baseDiffuseRoughness;
        float eV = Table(NoV, sigma).w, eL = Table(NoL, sigma).w, eAvg = Average(sigma).y;
        float3 rho = p.diffuseRho;
        float3 rhoMs = rho * rho * eAvg / Max(new float3(1.0f) - rho * (1.0f - eAvg), new float3(1e-4f));
        float3 diffuse = (rho * FonLobe(NoV, NoL, VoL, sigma) + rhoMs * ((1.0f - eV) * (1.0f - eL) / Max(1.0f - eAvg, 1e-4f))) * InvPi;
        diffuse *= Saturate(new float3(1.0f) - dielectricAlbedo);
        float3 baseSpecular = metal * s.baseMetalness + dielectric * (1.0f - s.baseMetalness);
        float3 baseDiffuse = diffuse * (1.0f - s.baseMetalness);

        // The coat over the base.
        float3 through = BaseThroughCoat(s, p, NoV);
        r.diffuse = baseDiffuse * through * NoL;
        r.specular = baseSpecular * through * NoL;
        if (s.coatWeight > 0.0f)
        {
            float rc = Max(s.coatRoughness, MinRoughness);
            float4 tc = Table(NoV, rc);
            float3 f0c = new float3(p.coatF0);
            float3 coat = FresnelSchlick(f0c, VoH) * SpecularDV(s, rc, v, l, h, NoV, NoL, NoH, false) * MultipleScattering(f0c, tc.x + tc.y);
            r.specular += coat * s.coatWeight * NoL;
        }
        return r;
    }

    /// <summary>The directional albedo seen from v: the light of a uniform environment of radiance 1 (the split of
    /// diffuse and specular for image based lighting: the diffuse part, the base specular and the coat).</summary>
    public static OpenPBRAlbedo Albedo(OpenPBRSurface s, float3 v)
    {
        Parts p = PartsOf(s);
        float NoV = Max(Dot(s.normal, v), 1e-4f);
        float3 metal = MetalAlbedo(p.metalF0, p.metalK, NoV, p.baseRoughness);
        float3 dielectric = p.dielectricTint * DielectricAlbedo(p.dielectricF0, NoV, p.baseRoughness);
        float3 diffuse = DiffuseAlbedo(p.diffuseRho, NoV, s.baseDiffuseRoughness) * Saturate(new float3(1.0f) - dielectric);
        float3 through = BaseThroughCoat(s, p, NoV);
        OpenPBRAlbedo r;
        r.diffuse = diffuse * (1.0f - s.baseMetalness) * through;
        r.specular = (metal * s.baseMetalness + dielectric * (1.0f - s.baseMetalness)) * through;
        r.coat = CoatAlbedo(s, p, NoV);
        return r;
    }

    /// <summary>The roughness the base's specular reflection of the environment is filtered with.</summary>
    public static float EnvironmentRoughness(OpenPBRSurface s) => PartsOf(s).baseRoughness;
}
