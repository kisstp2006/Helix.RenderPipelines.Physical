#pragma once

#include <Helix/Core/Containers/HashMap.h>
#include <Helix/Engine/Shader/ComputeShader.h>
#include <Helix/Engine/Texture/Texture.h>
#include <Helix/RenderPipelines/Core/RenderGraph/RenderGraph.h>
#include <Helix/RenderPipelines/Physical/Exposure/ExposureControl.h>
#include <Helix/RenderPipelines/Physical/Lighting/LightLoop.h>
#include <Helix/RenderPipelines/Physical/Pipeline/PhysicalRenderPipelineAsset.h>
#include <Helix/RenderPipelines/Physical/Temporal/TemporalAA.h>

#include <memory>

// The Physical pipeline: what the engine renders the cameras with while a PhysicalRenderPipelineAsset is set. A camera's
// frame on the core's render graph: the sun's cascaded shadows, the depth and normal prepass, the light clusters
// (compute), the opaque surfaces lit by their cluster's lights, the sky, the transparent surfaces, the automatic
// exposure's histogram (compute), TAA and the tonemapped result in the camera's target. The frame is pre-exposed:
// radiance in physical units times the camera's exposure.
namespace helix::rendering::physical
{
    // What a camera's last frame drew (the tests and the debug display read it).
    HX_STRUCT(Namespace = "HelixEngine.Rendering.Physical", Internal)
    struct PhysicalFrameInfo
    {
        HX_FIELD() i32 punctualLights = 0;
        HX_FIELD() i32 directionalLights = 0;
        HX_FIELD() i32 clustersX = 0;
        HX_FIELD() i32 clustersY = 0;
        HX_FIELD() i32 clustersZ = 0;
        HX_FIELD() bool shadows = false;
        HX_FIELD() bool temporalAntialiasing = false;
        HX_FIELD() bool bindless = false;
        // The EV100 written before the frame (the fixed and physical modes, the automatic mode's start).
        HX_FIELD() f32 ev100 = 0;
    };

    class PhysicalRenderPipeline final : public engine::RenderPipeline
    {
    public:
        // The value of the RenderPipeline tag of the SubShaders it draws.
        static constexpr StringView k_ShaderTagName = "PhysicalPipeline";
        // The pipeline asset in use when it is this pipeline's (else null).
        static PhysicalRenderPipelineAsset* asset();
        // Compute shaders and storage buffers in every stage (D3D11 feature level 11, OpenGL 4.5): the devices it draws
        // on. False with the reason for others.
        static bool SupportsDevice(String* reason = nullptr);
        // Whether the materials' textures go through the bindless tables on this device.
        static bool UsesBindless(const PhysicalRenderPipelineAsset* asset);
        // The last frame of the last camera.
        static const PhysicalFrameInfo& LastFrame();
        // A camera's exposure as the shaders read it (from the device: the automatic exposure's too), the factor and
        // its EV100; false without the pipeline or a frame of the camera.
        static bool ReadExposure(const engine::Camera* camera, f32& exposure, f32& ev100);

        explicit PhysicalRenderPipeline(PhysicalRenderPipelineAsset* asset);
        ~PhysicalRenderPipeline() override;

        void Render(engine::ScriptableRenderContext& context, Span<engine::Camera* const> cameras) override;

        PhysicalRenderPipelineAsset* pipelineAsset() const { return asset_.Get(); }

    protected:
        void Dispose(bool disposing) override;

    private:
        // The state a camera keeps between frames.
        struct CameraState
        {
            ExposureState exposure;
            TemporalState temporal;
            u32 frame = 0;
            Vector3 lastPosition;
            bool seen = false;
        };

        void RenderCamera(engine::ScriptableRenderContext& context, engine::Camera* camera);
        void EnsureResources();
        CameraState& StateOf(engine::Camera* camera);

        engine::PPtr<PhysicalRenderPipelineAsset> asset_;
        RenderGraph graph_;
        LightLoop lights_;
        HashMap<u64, std::unique_ptr<CameraState>> cameras_;
        // The energy tables (OpenPBR's albedo scaling and compensation) and the pipeline's own shaders.
        engine::PPtr<engine::Texture2D> energyTable_, energyAverage_;
        engine::PPtr<engine::Material> sky_, temporal_, final_;
        bool errorLogged_ = false;
    };

    // What C#'s PhysicalRenderPipeline calls.
    HX_CLASS(Namespace = "HelixEngine.Rendering.Physical", Static, Internal)
    class PhysicalRenderPipelineNative
    {
    public:
        HX_FUNCTION() static PhysicalRenderPipelineAsset* Asset() { return PhysicalRenderPipeline::asset(); }
        HX_FUNCTION() static bool SupportsDevice() { return PhysicalRenderPipeline::SupportsDevice(); }
        HX_FUNCTION() static PhysicalFrameInfo LastFrame() { return PhysicalRenderPipeline::LastFrame(); }
    };
}
