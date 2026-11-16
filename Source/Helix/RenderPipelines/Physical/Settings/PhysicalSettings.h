#pragma once

#include <Helix/Core/Basics/Types.h>
#include <Helix/Core/Strings/String.h>
#include <Helix/Scripting/Bindings/Tags.h>

// The settings of the Physical pipeline as its asset, its lights, cameras and volume components name them.
namespace helix::rendering::physical
{
    // The unit a light's intensity is given in.
    HX_ENUM(Namespace = "HelixEngine.Rendering.Physical")
    enum class LightUnit : i32
    {
        // Luminous power (punctual lights).
        Lumen = 0,
        // Luminous intensity (punctual lights).
        Candela = 1,
        // Illuminance (directional lights).
        Lux = 2,
        // Luminance (lights with a shape).
        Nits = 3,
        // Luminance as an exposure value (lights with a shape).
        Ev100 = 4,
    };

    // Where a camera's exposure comes from.
    HX_ENUM(Namespace = "HelixEngine.Rendering.Physical")
    enum class ExposureMode : i32
    {
        // The volume's fixed EV100.
        Fixed = 0,
        // The frame's luminance histogram, adapting over time.
        Automatic = 1,
        // The camera's aperture, shutter speed and ISO.
        UsePhysicalCamera = 2,
    };

    // How the automatic exposure reaches its target.
    HX_ENUM(Namespace = "HelixEngine.Rendering.Physical")
    enum class AdaptationMode : i32
    {
        Fixed = 0,
        Progressive = 1,
    };

    HX_ENUM(Namespace = "HelixEngine.Rendering.Physical")
    enum class TonemappingMode : i32
    {
        None = 0,
        Neutral = 1,
        ACES = 2,
    };

    // A camera's antialiasing.
    HX_ENUM(Namespace = "HelixEngine.Rendering.Physical")
    enum class AntialiasingMode : i32
    {
        None = 0,
        TemporalAntialiasing = 1,
    };

    // A layer mask in files: its bits (serializedVersion 2).
    HX_STRUCT(Namespace = "HelixEngine.Rendering.Physical", Internal)
    struct LayerMaskBits
    {
        static constexpr StringView kTypeName = "BitField";
        static constexpr i32 kVersion = 2;
        HX_FIELD(Name = "m_Bits") u32 bits = ~0u;

        template <class V>
        void Transfer(V& v)
        {
            v.Field("m_Bits", bits);
        }
    };

    // The size of the sun's shadow map in texels.
    HX_ENUM(Namespace = "HelixEngine.Rendering.Physical")
    enum class ShadowResolution : i32
    {
        _512 = 512,
        _1024 = 1024,
        _2048 = 2048,
        _4096 = 4096,
    };
}
