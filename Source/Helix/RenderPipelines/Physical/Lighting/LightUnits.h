#pragma once

#include <Helix/Engine/Rendering/Light.h>
#include <Helix/RenderPipelines/Physical/Settings/PhysicalSettings.h>

// Photometric units and exposure: the conversions between the units a light's intensity is given in, and the exposure
// of a camera (EV100 at ISO 100 with the reflected light meter's K = 12.5 and the saturation based sensitivity).
namespace helix::rendering::physical
{
    HX_CLASS(Namespace = "HelixEngine.Rendering.Physical", Static)
    class LightUnitUtils
    {
    public:
        // The solid angle a punctual light shines into: a point light the whole sphere, a spot light too (no reflector:
        // the cone masks a point light's intensity).
        HX_FUNCTION() static f32 SolidAngle(engine::LightType type);
        HX_FUNCTION() static f32 LumenToCandela(f32 lumen, f32 solidAngle) { return solidAngle > 0 ? lumen / solidAngle : 0.0f; }
        HX_FUNCTION() static f32 CandelaToLumen(f32 candela, f32 solidAngle) { return candela * solidAngle; }
        // Illuminance at a distance and the intensity that gives it (inverse square).
        HX_FUNCTION() static f32 CandelaToLux(f32 candela, f32 distance) { return distance > 0 ? candela / (distance * distance) : 0.0f; }
        HX_FUNCTION() static f32 LuxToCandela(f32 lux, f32 distance) { return lux * distance * distance; }
        // Luminance as EV100 and back: L = 2^EV100 * K / 100.
        HX_FUNCTION() static f32 Ev100ToNits(f32 ev100);
        HX_FUNCTION() static f32 NitsToEv100(f32 nits);
        // A sphere light of a radius seen from afar: its intensity is its luminance times its disc's area.
        HX_FUNCTION() static f32 NitsToCandela(f32 nits, f32 radius) { return nits * 3.14159265f * radius * radius; }
        HX_FUNCTION() static f32 CandelaToNits(f32 candela, f32 radius) { return radius > 0 ? candela / (3.14159265f * radius * radius) : 0.0f; }
        // A punctual light's intensity in candela, a directional light's illuminance in lux, from a value in a unit
        // (units that do not fit the light type give 0).
        HX_FUNCTION() static f32 ToShaderIntensity(engine::LightType type, LightUnit unit, f32 value, f32 radius);
        // A value of a unit in another (through candela or lux; 0 when they do not convert for the light type).
        HX_FUNCTION() static f32 ConvertIntensity(engine::LightType type, LightUnit from, LightUnit to, f32 value, f32 radius);
        // The unit a light type's intensity is given in by default (lux for directional lights, else candela).
        HX_FUNCTION() static LightUnit DefaultUnit(engine::LightType type) { return type == engine::LightType::Directional ? LightUnit::Lux : LightUnit::Candela; }

        // ------------------------------------------------------------------------------------------ exposure
        // The exposure value at ISO 100 of an aperture (f-number), a shutter time (seconds) and a sensitivity.
        HX_FUNCTION() static f32 ComputeEV100(f32 aperture, f32 shutterSpeed, f32 iso);
        // The factor a pre-exposed frame is drawn with: 1 / (1.2 * 2^EV100) (a luminance of 2^EV100 * 0.125 maps to
        // middle grey without saturating).
        HX_FUNCTION() static f32 EV100ToExposure(f32 ev100);
        HX_FUNCTION() static f32 ExposureToEV100(f32 exposure);
        // The EV100 that maps an average scene luminance (nits) to middle grey.
        HX_FUNCTION() static f32 AverageLuminanceToEV100(f32 nits);
        // A linear RGB color of a correlated color temperature (Kelvin), its luminance 1.
        HX_FUNCTION() static Color ColorTemperatureToLinear(f32 kelvin);
    };
}
