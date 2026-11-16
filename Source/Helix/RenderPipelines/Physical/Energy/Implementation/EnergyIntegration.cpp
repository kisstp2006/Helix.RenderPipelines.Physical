#include <Helix/RenderPipelines/Physical/Energy/EnergyIntegration.h>

#include <cmath>

namespace helix::rendering::physical::energy
{
    namespace
    {
        constexpr f64 kPi = 3.14159265358979323846;

        // The radical inverse in base 2 (the second coordinate of the Hammersley points).
        f64 RadicalInverse(u32 bits)
        {
            bits = (bits << 16u) | (bits >> 16u);
            bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
            bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
            bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
            bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
            return f64(bits) * 2.3283064365386963e-10;
        }

        // Gauss-Legendre nodes and weights on [0, 1] (64 points, by Newton's method on the Legendre polynomial).
        struct Legendre
        {
            static constexpr i32 kCount = 64;
            f64 x[kCount], w[kCount];

            Legendre()
            {
                const i32 n = kCount;
                for (i32 i = 0; i < n; i++)
                {
                    f64 z = std::cos(kPi * (f64(i) + 0.75) / (f64(n) + 0.5)), dp = 0;
                    for (i32 iteration = 0; iteration < 100; iteration++)
                    {
                        f64 p0 = 1, p1 = 0;
                        for (i32 k = 1; k <= n; k++)
                        {
                            f64 p2 = p1;
                            p1 = p0;
                            p0 = ((2.0 * k - 1.0) * z * p1 - (k - 1.0) * p2) / k;
                        }
                        dp = n * (z * p0 - p1) / (z * z - 1.0);
                        f64 dz = p0 / dp;
                        z -= dz;
                        if (std::fabs(dz) < 1e-15)
                            break;
                    }
                    x[i] = 0.5 * (1.0 - z);
                    w[i] = 1.0 / ((1.0 - z * z) * dp * dp);
                }
            }
        };

        const Legendre& Nodes()
        {
            static const Legendre l;
            return l;
        }
    }

    f64 NodeCosine(i32 i)
    {
        f64 mu = f64(i) / f64(kSize - 1);
        return mu < 1e-4 ? 1e-4 : mu;
    }

    f64 NodeRoughness(i32 j) { return f64(j) / f64(kSize - 1); }

    f64 SmithG2(f64 muV, f64 muL, f64 alpha)
    {
        auto lambda = [&](f64 mu) {
            f64 mu2 = mu * mu;
            return 0.5 * (-1.0 + std::sqrt(1.0 + alpha * alpha * (1.0 - mu2) / mu2));
        };
        return 1.0 / (1.0 + lambda(muV) + lambda(muL));
    }

    GgxAlbedo IntegrateGgx(f64 mu, f64 roughness, u32 samples)
    {
        f64 r = roughness < kMinRoughness ? kMinRoughness : roughness;
        f64 alpha = r * r;
        // The view in the xz plane; half vectors from the distribution of visible normals' parent (D times cos).
        f64 vx = std::sqrt(1.0 - mu * mu), vz = mu;
        GgxAlbedo out;
        for (u32 s = 0; s < samples; s++)
        {
            f64 u1 = (f64(s) + 0.5) / f64(samples), u2 = RadicalInverse(s);
            f64 phi = 2.0 * kPi * u2;
            f64 cosH = std::sqrt((1.0 - u1) / (1.0 + (alpha * alpha - 1.0) * u1));
            f64 sinH = std::sqrt(1.0 - cosH * cosH);
            f64 hx = sinH * std::cos(phi), hz = cosH;
            f64 vh = vx * hx + vz * hz;
            if (vh <= 0)
                continue;
            // The reflected direction's cosine with the normal.
            f64 lz = 2.0 * vh * hz - vz;
            if (lz <= 0)
                continue;
            // f cos / pdf = G2 (v.h) / ((n.v) (n.h)).
            f64 weight = SmithG2(mu, lz, alpha) * vh / (mu * hz);
            f64 fc = std::pow(1.0 - vh, 5.0);
            out.a += (1.0 - fc) * weight;
            out.b += fc * weight;
            out.c += vh * std::pow(1.0 - vh, 6.0) * weight;
        }
        out.a /= f64(samples);
        out.b /= f64(samples);
        out.c /= f64(samples);
        return out;
    }

    void FonConstants(f64 sigma, f64& a, f64& b)
    {
        constexpr f64 c1 = 0.5 - 2.0 / (3.0 * kPi);
        a = 1.0 / (1.0 + c1 * sigma);
        b = sigma * a;
    }

    f64 IntegrateFon(f64 mu, f64 sigma)
    {
        f64 a, b;
        FonConstants(sigma, a, b);
        const Legendre& l = Nodes();
        f64 sinO = std::sqrt(1.0 - mu * mu);
        // E = A + B / pi * the integral of s / t * cos over the hemisphere (s = sin sin cos phi).
        constexpr i32 kPhi = 256;
        f64 sum = 0;
        for (i32 i = 0; i < Legendre::kCount; i++)
        {
            f64 muI = l.x[i], sinI = std::sqrt(1.0 - muI * muI);
            f64 inner = 0;
            for (i32 p = 0; p < kPhi; p++)
            {
                f64 phi = 2.0 * kPi * (f64(p) + 0.5) / f64(kPhi);
                f64 s = sinI * sinO * std::cos(phi);
                f64 t = s > 0 ? (muI > mu ? muI : mu) : 1.0;
                inner += s / t;
            }
            inner *= 2.0 * kPi / f64(kPhi);
            sum += l.w[i] * inner * muI;
        }
        return a + b / kPi * sum;
    }

    f64 AverageGgx(f64 roughness, u32 samples)
    {
        const Legendre& l = Nodes();
        f64 sum = 0;
        for (i32 i = 0; i < Legendre::kCount; i++)
        {
            GgxAlbedo e = IntegrateGgx(l.x[i] < 1e-4 ? 1e-4 : l.x[i], roughness, samples);
            sum += l.w[i] * (e.a + e.b) * l.x[i];
        }
        return 2.0 * sum;
    }

    f64 AverageFon(f64 sigma)
    {
        const Legendre& l = Nodes();
        f64 sum = 0;
        for (i32 i = 0; i < Legendre::kCount; i++)
            sum += l.w[i] * IntegrateFon(l.x[i], sigma) * l.x[i];
        return 2.0 * sum;
    }
}
