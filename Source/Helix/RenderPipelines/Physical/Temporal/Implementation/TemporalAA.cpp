#include <Helix/RenderPipelines/Physical/Temporal/TemporalAA.h>

#include <Helix/Engine/Shader/Shader.h>
#include <Helix/RenderPipelines/Core/Utilities/Blitter.h>

namespace helix::rendering::physical
{
    using namespace engine;

    namespace
    {
        f32 Halton(u32 index, u32 base)
        {
            f32 f = 1, r = 0;
            while (index > 0)
            {
                f /= f32(base);
                r += f * f32(index % base);
                index /= base;
            }
            return r;
        }

        struct TaaData
        {
            Material* material = nullptr;
            TextureHandle color, depth, history, output;
            RenderGraph* graph = nullptr;
            Matrix4x4 inverseViewProjection, previousViewProjection;
            Vector4 params;
            Vector4 screen;
        };
    }

    void TemporalState::Release()
    {
        for (RTHandle*& h : history)
        {
            if (h && RTHandle::Find(h->Id()))
                h->Release();
            h = nullptr;
        }
        width = height = 0;
        valid = false;
    }

    Vector2 TemporalAA::JitterOffset(u32 frame)
    {
        // Halton points from index 1 (index 0 is the corner), centred on the pixel.
        u32 i = frame % kSampleCount + 1;
        return Vector2(Halton(i, 2) - 0.5f, Halton(i, 3) - 0.5f);
    }

    Matrix4x4 TemporalAA::JitterProjection(const Matrix4x4& projection, Vector2 offsetPixels, i32 width, i32 height)
    {
        Matrix4x4 p = projection;
        // A pixel is 2 / size in clip space; the offset moves x and y after the divide by w (the third column).
        p.m02 += offsetPixels.x * 2.0f / f32(Max(width, 1));
        p.m12 += offsetPixels.y * 2.0f / f32(Max(height, 1));
        return p;
    }

    bool TemporalAA::Prepare(TemporalState& state, i32 width, i32 height, GraphicsFormat format)
    {
        if (state.history[0] && state.history[1] && state.width == width && state.height == height)
            return true;
        state.Release();
        RTHandleAllocInfo info;
        info.format = format;
        info.filterMode = FilterMode::Bilinear;
        info.wrapMode = TextureWrapMode::Clamp;
        info.name = String("_TaaHistory");
        for (RTHandle*& h : state.history)
            h = RTHandles::DefaultInstance().Alloc(width, height, info);
        state.width = width;
        state.height = height;
        state.current = 0;
        state.valid = false;
        return state.history[0] && state.history[1];
    }

    TextureHandle TemporalAA::Record(RenderGraph& graph, Material* material, TemporalState& state, TextureHandle color, TextureHandle depth,
                                     const Matrix4x4& inverseViewProjection, f32 historyWeight, Vector2 jitter, bool reset)
    {
        if (!material || !state.history[0] || !state.history[1])
            return color;
        TaaData* data = nullptr;
        PassBuilder<TaaData> b = graph.AddRasterRenderPass("TemporalAA", data);
        data->material = material;
        data->graph = &graph;
        data->color = color;
        data->depth = depth;
        // The previous frame's resolve is read, the other texture written (and read by the next frame).
        data->history = graph.ImportTexture(state.history[state.current]);
        data->output = graph.ImportTexture(state.history[1 - state.current]);
        data->inverseViewProjection = inverseViewProjection;
        data->previousViewProjection = state.previousViewProjection;
        bool fresh = reset || !state.valid;
        data->params = Vector4(fresh ? 0.0f : historyWeight, fresh ? 1.0f : 0.0f, jitter.x / f32(Max(state.width, 1)), jitter.y / f32(Max(state.height, 1)));
        data->screen = Vector4(f32(state.width), f32(state.height), 1.0f / f32(Max(state.width, 1)), 1.0f / f32(Max(state.height, 1)));
        b.UseTexture(color, AccessFlags::Read);
        b.UseTexture(depth, AccessFlags::Read);
        b.UseTexture(data->history, AccessFlags::Read);
        b.SetRenderAttachment(data->output, 0, AccessFlags::WriteAll);
        b.SetRenderFunc([](TaaData& d, RasterGraphContext& ctx) {
            static const i32 colorId = Shader::PropertyToID("_TaaColor"), historyId = Shader::PropertyToID("_TaaHistory"),
                             depthId = Shader::PropertyToID("_TaaDepth"), inverseId = Shader::PropertyToID("_TaaInverseViewProjection"),
                             previousId = Shader::PropertyToID("_TaaPreviousViewProjection"), paramsId = Shader::PropertyToID("_TaaParams"),
                             screenId = Shader::PropertyToID("_TaaScreen");
            RTHandle *c = d.graph->Resolve(d.color), *h = d.graph->Resolve(d.history), *z = d.graph->Resolve(d.depth);
            if (!c || !h || !z)
                return;
            // The values ride in a property block (the draw keeps its copy).
            MaterialPropertyBlock block;
            block.SetTexture(colorId, c->rt());
            block.SetTexture(historyId, h->rt());
            block.SetTexture(depthId, z->rt());
            block.SetMatrix(inverseId, d.inverseViewProjection);
            block.SetMatrix(previousId, d.previousViewProjection);
            block.SetVector(paramsId, d.params);
            block.SetVector(screenId, d.screen);
            ctx.cmd->DrawProcedural(Matrix4x4::identity, d.material, 0, MeshTopology::Triangles, 3, 1, &block);
        });
        b.Dispose();
        return data->output;
    }

    void TemporalAA::Finish(TemporalState& state, const Matrix4x4& viewProjection)
    {
        state.current = 1 - state.current;
        state.previousViewProjection = viewProjection;
        state.valid = true;
        state.frame++;
    }
}
