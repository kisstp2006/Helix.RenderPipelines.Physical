#pragma once

#include <Helix/Core/Basics/Types.h>

// The integrals behind the OpenPBR energy tables, computed by Tools/EnergyTables (which writes EnergyTables.inl) and
// checked by the tests: the directional albedo of the GGX lobe with height correlated Smith masking split by the
// Fresnel terms (A: the share of F0, B: the share of the Schlick term, C: the F82 tint's term), the directional albedo
// of the Fujii Oren-Nayar lobe of the EON diffuse model, and their averages over the hemisphere.
namespace helix::rendering::physical::energy
{
    // The tables' size: kSize values of the cosine (0 to 1) by kSize roughnesses (0 to 1).
    inline constexpr i32 kSize = 32;
    // The smallest roughness the GGX integrals take (a mirror's albedo is its limit).
    inline constexpr f64 kMinRoughness = 0.02;

    // The cosine and the roughness of a table node.
    f64 NodeCosine(i32 i);
    f64 NodeRoughness(i32 j);

    struct GgxAlbedo
    {
        // A + B is the albedo with a Fresnel of 1; F0 * A + B with Schlick's; - c * C the F82 tint's correction.
        f64 a = 0, b = 0, c = 0;
    };

    // GGX of roughness r (alpha = r * r) seen at cosine mu: importance sampled over `samples` half vectors.
    GgxAlbedo IntegrateGgx(f64 mu, f64 roughness, u32 samples = 16384);
    // The Fujii Oren-Nayar lobe of roughness sigma with an albedo of 1 seen at cosine mu (quadrature).
    f64 IntegrateFon(f64 mu, f64 sigma);
    // 2 * the integral of E(mu) mu over mu: the albedo averaged over the incoming directions.
    f64 AverageGgx(f64 roughness, u32 samples = 4096);
    f64 AverageFon(f64 sigma);

    // The FON lobe's constants (A, B of the model).
    void FonConstants(f64 sigma, f64& a, f64& b);
    // Height correlated Smith masking and shadowing of GGX.
    f64 SmithG2(f64 muV, f64 muL, f64 alpha);
}
