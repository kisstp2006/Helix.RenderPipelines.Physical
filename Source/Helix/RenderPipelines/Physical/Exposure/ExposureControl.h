#pragma once

#include <Helix/Engine/Rendering/Camera.h>
#include <Helix/Engine/Rendering/GraphicsBuffer.h>
#include <Helix/Engine/Rendering/Pipeline/CommandBuffer.h>
#include <Helix/Engine/Shader/ComputeShader.h>
#include <Helix/RenderPipelines/Core/RenderGraph/RenderGraph.h>
#include <Helix/RenderPipelines/Physical/Overrides/Overrides.h>

// A camera's exposure: one value the shaders read from a buffer (_PhysicalExposure: x the factor the frame is drawn
// with, y its EV100), so the frame is pre-exposed. The fixed and the physical camera's exposure are written before
// the frame; the automatic exposure is the luminance histogram of the frame (compute), its average between the
// percentages adapting over time, used by the next frame.
namespace helix::rendering::physical
{
    // A camera's exposure buffer and whether it holds a value yet.
    struct ExposureState
    {
        engine::GraphicsBuffer* buffer = nullptr;
        bool initialized = false;
        // The EV100 written before the frame (the fixed and physical modes; the automatic mode's starting value).
        f32 ev100 = 0;

        ~ExposureState() { engine::GraphicsBuffer::Release(buffer); }
        ExposureState() = default;
        ExposureState(const ExposureState&) = delete;
        ExposureState& operator=(const ExposureState&) = delete;
    };

    // The histogram's bins.
    inline constexpr i32 kExposureHistogramBins = 128;

    struct ExposureControl
    {
        // The EV100 of a mode that does not measure the frame (fixed, the physical camera) with the compensation.
        static f32 TargetEV100(const Exposure& exposure, const engine::Camera& camera);
        // The buffer made, the frame's exposure written when the mode knows it (or the automatic mode starts), the
        // buffer set as the global _PhysicalExposure.
        static void Setup(engine::CommandBuffer& cmd, ExposureState& state, const Exposure& exposure, const engine::Camera& camera, bool reset);
        // The automatic mode's passes after the frame: the histogram of `color` and the new exposure.
        static void RecordAutomatic(RenderGraph& graph, engine::ComputeShader* shader, ExposureState& state, TextureHandle color, const Exposure& exposure,
                                    i32 width, i32 height, f32 deltaTime, bool reset);
    };
}
