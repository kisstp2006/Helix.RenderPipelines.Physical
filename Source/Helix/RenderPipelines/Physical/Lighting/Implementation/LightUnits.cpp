#include <Helix/RenderPipelines/Physical/Lighting/LightUnits.h>

#include <Helix/Math/Color.h>

#include <cmath>

namespace helix::rendering::physical
{
    namespace
    {
        constexpr f32 kPi = 3.14159265358979f;
        // The reflected light meter's calibration constant and the saturation based sensitivity's 78 / (0.65 * 100).
        constexpr f32 kMeterK = 12.5f;
        constexpr f32 kSaturation = 1.2f;

        bool IsPunctual(engine::LightType type) { return type == engine::LightType::Point || type == engine::LightType::Spot; }
    }

    f32 LightUnitUtils::SolidAngle(engine::LightType type) { return IsPunctual(type) ? 4.0f * kPi : 0.0f; }

    f32 LightUnitUtils::Ev100ToNits(f32 ev100) { return std::exp2(ev100) * kMeterK / 100.0f; }

    f32 LightUnitUtils::NitsToEv100(f32 nits) { return nits > 0 ? std::log2(nits * 100.0f / kMeterK) : -100.0f; }

    f32 LightUnitUtils::ToShaderIntensity(engine::LightType type, LightUnit unit, f32 value, f32 radius)
    {
        if (type == engine::LightType::Directional)
            return unit == LightUnit::Lux ? value : 0.0f;
        if (!IsPunctual(type))
            return 0.0f;
        switch (unit)
        {
        case LightUnit::Candela: return value;
        case LightUnit::Lumen: return LumenToCandela(value, SolidAngle(type));
        case LightUnit::Nits: return NitsToCandela(value, radius);
        case LightUnit::Ev100: return NitsToCandela(Ev100ToNits(value), radius);
        default: return 0.0f;
        }
    }

    f32 LightUnitUtils::ConvertIntensity(engine::LightType type, LightUnit from, LightUnit to, f32 value, f32 radius)
    {
        if (from == to)
            return value;
        if (type == engine::LightType::Directional)
            return 0.0f;
        f32 candela = ToShaderIntensity(type, from, value, radius);
        switch (to)
        {
        case LightUnit::Candela: return candela;
        case LightUnit::Lumen: return CandelaToLumen(candela, SolidAngle(type));
        case LightUnit::Nits: return CandelaToNits(candela, radius);
        case LightUnit::Ev100: return NitsToEv100(CandelaToNits(candela, radius));
        default: return 0.0f;
        }
    }

    f32 LightUnitUtils::ComputeEV100(f32 aperture, f32 shutterSpeed, f32 iso)
    {
        if (shutterSpeed <= 0 || iso <= 0)
            return 0.0f;
        return std::log2(aperture * aperture / shutterSpeed * 100.0f / iso);
    }

    f32 LightUnitUtils::EV100ToExposure(f32 ev100) { return 1.0f / (kSaturation * std::exp2(ev100)); }

    f32 LightUnitUtils::ExposureToEV100(f32 exposure) { return exposure > 0 ? std::log2(1.0f / (kSaturation * exposure)) : 0.0f; }

    f32 LightUnitUtils::AverageLuminanceToEV100(f32 nits) { return nits > 0 ? std::log2(nits * 100.0f / kMeterK) : -10.0f; }

    Color LightUnitUtils::ColorTemperatureToLinear(f32 kelvin)
    {
        Color c = mathf::CorrelatedColorTemperatureToRGB(kelvin);
        // Rec. 709 luminance 1: the temperature tints the light, its intensity stays the unit's.
        f32 y = 0.2126729f * c.r + 0.7151522f * c.g + 0.0721750f * c.b;
        return y > 0 ? Color(c.r / y, c.g / y, c.b / y, 1.0f) : Color(1, 1, 1, 1);
    }
}
