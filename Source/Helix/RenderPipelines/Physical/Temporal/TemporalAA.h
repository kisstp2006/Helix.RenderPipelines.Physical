#pragma once

#include <Helix/Engine/Material/Material.h>
#include <Helix/Math/Matrix4x4.h>
#include <Helix/RenderPipelines/Core/RenderGraph/RenderGraph.h>
#include <Helix/RenderPipelines/Core/Textures/RTHandle.h>

// Temporal antialiasing: the projection jittered by a Halton (2, 3) sequence of sub-pixel offsets, the history of the
// resolved frames reprojected by the camera's motion (each pixel's position from the depth, seen by the previous
// frame's view and projection), its colors clamped to the current frame's neighbourhood, blended with the new frame.
// The history starts anew on a cut, a new size and the first frame.
namespace helix::rendering::physical
{
    // A camera's history.
    struct TemporalState
    {
        RTHandle* history[2] = {nullptr, nullptr};
        i32 current = 0;
        i32 width = 0, height = 0;
        u32 frame = 0;
        // The previous frame's view and projection without the jitter; whether it is there.
        Matrix4x4 previousViewProjection = Matrix4x4::identity;
        bool valid = false;

        ~TemporalState() { Release(); }
        TemporalState() = default;
        TemporalState(const TemporalState&) = delete;
        TemporalState& operator=(const TemporalState&) = delete;
        void Release();
    };

    struct TemporalAA
    {
        // The sequence's length.
        static constexpr u32 kSampleCount = 8;
        // The offset of a frame in pixels (-0.5 to 0.5 each way).
        static Vector2 JitterOffset(u32 frame);
        // A projection moved by an offset in pixels of a target's size.
        static Matrix4x4 JitterProjection(const Matrix4x4& projection, Vector2 offsetPixels, i32 width, i32 height);
        // The textures of the state for a size (made again when it changed; the history then starts anew).
        static bool Prepare(TemporalState& state, i32 width, i32 height, engine::GraphicsFormat format);
        // The resolve pass into the next history texture (imported); the resolved color, which is the history the next
        // frame reads. `reset` starts it anew (the frame alone).
        static TextureHandle Record(RenderGraph& graph, engine::Material* material, TemporalState& state, TextureHandle color, TextureHandle depth,
                                    const Matrix4x4& inverseViewProjection, f32 historyWeight, Vector2 jitter, bool reset);
        // After the frame: the history swapped, the view and projection kept for the next frame.
        static void Finish(TemporalState& state, const Matrix4x4& viewProjection);
    };
}
