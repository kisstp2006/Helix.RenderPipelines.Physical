#include <Helix/RenderPipelines/Physical/Exposure/ExposureControl.h>

#include <Helix/Engine/Shader/Shader.h>
#include <Helix/RenderPipelines/Physical/Lighting/LightUnits.h>

namespace helix::rendering::physical
{
    using namespace engine;

    f32 ExposureControl::TargetEV100(const Exposure& exposure, const Camera& camera)
    {
        f32 ev = exposure.fixedExposure.value();
        if (exposure.mode.value() == ExposureMode::UsePhysicalCamera)
            ev = LightUnitUtils::ComputeEV100(camera.aperture(), camera.shutterSpeed(), f32(camera.iso()));
        // A positive compensation brightens: a lower exposure value.
        return ev - exposure.compensation.value();
    }

    void ExposureControl::Setup(CommandBuffer& cmd, ExposureState& state, const Exposure& exposure, const Camera& camera, bool reset)
    {
        if (!state.buffer)
        {
            state.buffer = GraphicsBuffer::Create(GraphicsBufferTarget::Structured, 0, 1, 16);
            if (state.buffer)
                state.buffer->SetName("_PhysicalExposure");
            state.initialized = false;
        }
        if (!state.buffer)
            return;
        bool automatic = exposure.mode.value() == ExposureMode::Automatic;
        // The automatic mode starts from the fixed value within its limits and then measures.
        if (!automatic || reset || !state.initialized)
        {
            f32 ev = TargetEV100(exposure, camera);
            if (automatic)
                ev = Clamp(exposure.fixedExposure.value(), exposure.limitMin.value(), exposure.limitMax.value()) - exposure.compensation.value();
            state.ev100 = ev;
            f32 values[4] = {LightUnitUtils::EV100ToExposure(ev), ev, 0, 0};
            cmd.SetBufferData(state.buffer->Id(), values, sizeof(values), 0);
            state.initialized = true;
        }
        cmd.SetGlobalBuffer(Shader::PropertyToID("_PhysicalExposure"), state.buffer);
    }

    namespace
    {
        struct HistogramData
        {
            ComputeShader* shader = nullptr;
            i32 clear = -1, histogram = -1, adapt = -1;
            TextureHandle color;
            BufferHandle bins, exposure;
            RenderGraph* graph = nullptr;
            Vector4 params, speeds, size;
        };
    }

    void ExposureControl::RecordAutomatic(RenderGraph& graph, ComputeShader* shader, ExposureState& state, TextureHandle color, const Exposure& exposure,
                                          i32 width, i32 height, f32 deltaTime, bool reset)
    {
        if (!shader || !state.buffer || exposure.mode.value() != ExposureMode::Automatic)
            return;
        HistogramData* data = nullptr;
        PassBuilder<HistogramData> b = graph.AddComputePass("AutomaticExposure", data);
        data->shader = shader;
        data->clear = shader->KernelIndex("ClearHistogram");
        data->histogram = shader->KernelIndex("Histogram");
        data->adapt = shader->KernelIndex("Adapt");
        data->graph = &graph;
        data->color = color;
        BufferDesc binsDesc(kExposureHistogramBins, 4);
        binsDesc.name = String("_ExposureHistogram");
        data->bins = b.CreateTransientBuffer(binsDesc);
        data->exposure = graph.ImportBuffer(state.buffer);
        f32 lo = exposure.histogramLowPercentage.value() * 0.01f, hi = exposure.histogramHighPercentage.value() * 0.01f;
        data->params = Vector4(exposure.limitMin.value(), Max(exposure.limitMax.value(), exposure.limitMin.value() + 0.01f), Min(lo, hi), Max(lo, hi));
        bool instant = reset || exposure.adaptationMode.value() == AdaptationMode::Fixed;
        data->speeds = Vector4(exposure.compensation.value(), exposure.adaptationSpeedDarkToLight.value(), exposure.adaptationSpeedLightToDark.value(),
                               instant ? -1.0f : Max(deltaTime, 0.0f));
        data->size = Vector4(f32(width), f32(height), 0, 0);
        b.UseTexture(color, AccessFlags::Read);
        b.UseBuffer(data->exposure, AccessFlags::ReadWrite);
        // The exposure is read by the next frame: the pass is kept.
        b.AllowPassCulling(false);
        b.SetRenderFunc([](HistogramData& d, ComputeGraphContext& ctx) {
            if (d.clear < 0 || d.histogram < 0 || d.adapt < 0)
                return;
            static const i32 binsId = Shader::PropertyToID("_ExposureHistogram"), exposureId = Shader::PropertyToID("_PhysicalExposure"),
                             sourceId = Shader::PropertyToID("_ExposureSource"), paramsId = Shader::PropertyToID("_ExposureParams"),
                             speedsId = Shader::PropertyToID("_ExposureSpeeds"), sizeId = Shader::PropertyToID("_ExposureSourceSize");
            GraphicsBuffer* bins = d.graph->Resolve(d.bins);
            GraphicsBuffer* exposure = d.graph->Resolve(d.exposure);
            RTHandle* source = d.graph->Resolve(d.color);
            if (!bins || !exposure || !source)
                return;
            ctx.cmd->SetComputeVectorParam(d.shader, paramsId, d.params);
            ctx.cmd->SetComputeVectorParam(d.shader, speedsId, d.speeds);
            ctx.cmd->SetComputeVectorParam(d.shader, sizeId, d.size);
            for (i32 k : {d.clear, d.histogram, d.adapt})
            {
                ctx.cmd->SetComputeBufferParam(d.shader, k, binsId, bins);
                ctx.cmd->SetComputeBufferParam(d.shader, k, exposureId, exposure);
            }
            ctx.cmd->SetComputeTextureParam(d.shader, d.histogram, sourceId, source->nameID());
            ctx.cmd->DispatchCompute(d.shader, d.clear, (kExposureHistogramBins + 63) / 64, 1, 1);
            ctx.cmd->DispatchCompute(d.shader, d.histogram, (i32(d.size.x) + 7) / 8, (i32(d.size.y) + 7) / 8, 1);
            ctx.cmd->DispatchCompute(d.shader, d.adapt, 1, 1, 1);
        });
    }
}
