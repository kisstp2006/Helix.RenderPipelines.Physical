#pragma once

#include <Helix/Engine/Rendering/GraphicsBuffer.h>
#include <Helix/Engine/Rendering/Pipeline/CommandBuffer.h>
#include <Helix/Engine/Rendering/Pipeline/RenderStates.h>
#include <Helix/Engine/Shader/ComputeShader.h>
#include <Helix/RenderPipelines/Core/RenderGraph/RenderGraph.h>

// The lights of a camera's frame in physical units: the directional lights (illuminance in lux) as globals, the point
// and spot lights (intensity in candela) in a buffer, and their clusters: the view cut into screen tiles and
// logarithmic depth slices, each cluster listing the lights whose range reaches it (a compute pass); the forward
// shaders read their pixel's cluster.
namespace helix::rendering::physical
{
    class PhysicalRenderPipelineAsset;

    // A point or spot light as the shaders read it (64 bytes, std430).
    struct GpuPunctualLight
    {
        // xyz: world position, w: range (the light reaches no farther).
        Vector4 positionRange;
        // rgb: linear color times intensity (candela), w: the shape's radius (the smallest distance of the falloff).
        Vector4 colorRadius;
        // xyz: the direction the light shines in (spot lights), w: 1 for spot lights, 0 for point lights.
        Vector4 directionType;
        // x: the spot's angle scale, y: its offset (attenuation = saturate(cos * x + y)^2), z: 1 diffuse, w: 1 specular.
        Vector4 spot;
    };
    static_assert(sizeof(GpuPunctualLight) == 64);

    // The most directional lights a frame takes (the first one the brightest, with the cascaded shadows).
    inline constexpr i32 kMaxDirectionalLights = 4;

    struct LightLoopFrame
    {
        i32 punctualCount = 0;
        i32 directionalCount = 0;
        // The visible light index of the directional light with the shadows (-1: none).
        i32 mainLightIndex = -1;
        i32 clustersX = 0, clustersY = 0, clustersZ = 0;
        i32 maxPerCluster = 0;
    };

    class LightLoop
    {
    public:
        ~LightLoop();
        void Release();

        // The frame's lights from the culling into the light buffer and the globals (in the command buffer, before the
        // graph runs); the clusters' grid for the camera's pixel size.
        void Setup(engine::CommandBuffer& cmd, engine::CullingResults culling, const PhysicalRenderPipelineAsset& asset, i32 width, i32 height,
                   const Matrix4x4& view, const Matrix4x4& projection, f32 nearPlane, f32 farPlane);
        // The clustering pass: the cluster buffers written for the forward passes (globals after it).
        void RecordClustering(RenderGraph& graph, engine::ComputeShader* clustering);
        // The cluster buffers the forward passes read (handles of the frame being recorded).
        BufferHandle counts() const { return counts_; }
        BufferHandle indices() const { return indices_; }

        const LightLoopFrame& frame() const { return frame_; }

    private:
        engine::GraphicsBuffer* lights_ = nullptr;
        i32 capacity_ = 0;
        BufferHandle counts_, indices_;
        LightLoopFrame frame_;
    };
}
