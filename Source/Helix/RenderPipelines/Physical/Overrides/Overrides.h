#pragma once

#include <Helix/RenderPipelines/Core/Volume/VolumeComponent.h>
#include <Helix/RenderPipelines/Physical/Settings/PhysicalSettings.h>

// The Physical pipeline's volume components: the camera's exposure and the tonemapping of the pre-exposed frame.
namespace helix::rendering::physical
{
    HX_HANDLE(CSharp = "HelixEngine.Rendering.Physical.ExposureModeParameter")
    class ExposureModeParameter : public VolumeParameterOf<ExposureMode>
    {
    public:
        using VolumeParameterOf::VolumeParameterOf;
    };

    HX_HANDLE(CSharp = "HelixEngine.Rendering.Physical.AdaptationModeParameter")
    class AdaptationModeParameter : public VolumeParameterOf<AdaptationMode>
    {
    public:
        using VolumeParameterOf::VolumeParameterOf;
    };

    HX_HANDLE(CSharp = "HelixEngine.Rendering.Physical.TonemappingModeParameter")
    class TonemappingModeParameter : public VolumeParameterOf<TonemappingMode>
    {
    public:
        using VolumeParameterOf::VolumeParameterOf;
    };

    // The exposure of the frame (EV100): fixed, from the camera's physical properties, or from the frame's luminance
    // histogram (the share between the low and the high percentage averaged, clamped to the limits, reached over time).
    HX_CLASS(Namespace = "HelixEngine.Rendering.Physical", Sealed)
    class Exposure final : public VolumeComponent
    {
    public:
        Exposure();

        HX_FIELD() ExposureModeParameter mode{ExposureMode::Fixed};
        // EV100 of the Fixed mode.
        HX_FIELD() FloatParameter fixedExposure{15.0f};
        // EV added to the exposure of every mode (positive: brighter).
        HX_FIELD() FloatParameter compensation{0.0f};
        // The EV100 range the automatic exposure stays in.
        HX_FIELD() FloatParameter limitMin{-6.0f};
        HX_FIELD() FloatParameter limitMax{18.0f};
        // The darkest and the brightest share of the histogram left out of the average (percent).
        HX_FIELD() ClampedFloatParameter histogramLowPercentage{40.0f, 0.0f, 100.0f};
        HX_FIELD() ClampedFloatParameter histogramHighPercentage{90.0f, 0.0f, 100.0f};
        HX_FIELD() AdaptationModeParameter adaptationMode{AdaptationMode::Progressive};
        // EV per second toward a brighter (darker) scene.
        HX_FIELD() MinFloatParameter adaptationSpeedDarkToLight{3.0f, 0.001f};
        HX_FIELD() MinFloatParameter adaptationSpeedLightToDark{1.0f, 0.001f};
    };

    // The mapping of the exposed frame into the display's range.
    HX_CLASS(Namespace = "HelixEngine.Rendering.Physical", Sealed)
    class Tonemapping final : public VolumeComponent
    {
    public:
        Tonemapping();

        HX_FIELD() TonemappingModeParameter mode{TonemappingMode::None};

        HX_FUNCTION() bool IsActive() const { return mode.value() != TonemappingMode::None; }
    };
}
