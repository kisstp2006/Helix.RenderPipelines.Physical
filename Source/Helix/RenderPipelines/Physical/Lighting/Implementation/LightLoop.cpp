#include <Helix/RenderPipelines/Physical/Lighting/LightLoop.h>

#include <Helix/Engine/Rendering/Light.h>
#include <Helix/Engine/Shader/Shader.h>
#include <Helix/RenderPipelines/Physical/Components/AdditionalData.h>
#include <Helix/RenderPipelines/Physical/Lighting/LightUnits.h>
#include <Helix/RenderPipelines/Physical/Pipeline/PhysicalRenderPipelineAsset.h>

#include <algorithm>
#include <cmath>

namespace helix::rendering::physical
{
    using namespace engine;

    namespace
    {
        // A light's linear color with its temperature, times its intensity in the shaders' unit.
        Vector4 LinearColor(const Light& light, f32 intensity)
        {
            Color c = light.color();
            Color linear(mathf::GammaToLinearSpace(c.r), mathf::GammaToLinearSpace(c.g), mathf::GammaToLinearSpace(c.b), 1);
            if (light.useColorTemperature())
            {
                Color t = LightUnitUtils::ColorTemperatureToLinear(light.colorTemperature());
                linear = Color(linear.r * t.r, linear.g * t.g, linear.b * t.b, 1);
            }
            return Vector4(linear.r * intensity, linear.g * intensity, linear.b * intensity, 0);
        }

        f32 IntensityOf(const Light& light)
        {
            if (const PhysicalAdditionalLightData* d = FindPhysicalAdditionalLightData(&light))
                return d->ShaderIntensity(light.type());
            // Without the component the light's intensity is in its type's default unit.
            return light.intensity();
        }

        struct Directional
        {
            Vector4 direction, color;
            f32 brightness;
            i32 visibleIndex;
        };
    }

    LightLoop::~LightLoop() { Release(); }

    void LightLoop::Release()
    {
        GraphicsBuffer::Release(lights_);
        lights_ = nullptr;
        capacity_ = 0;
    }

    void LightLoop::Setup(CommandBuffer& cmd, CullingResults culling, const PhysicalRenderPipelineAsset& asset, i32 width, i32 height, const Matrix4x4& view,
                          const Matrix4x4& projection, f32 nearPlane, f32 farPlane)
    {
        frame_ = LightLoopFrame();
        Span<const VisibleLight> visible = culling.visibleLights();
        Array<Directional> directionals;
        Array<GpuPunctualLight> punctual;
        i32 limit = asset.maxVisibleLights();
        for (u32 i = 0; i < visible.Size(); i++)
        {
            const VisibleLight& v = visible[i];
            const Light* light = v.light();
            if (!light)
                continue;
            const PhysicalAdditionalLightData* extra = FindPhysicalAdditionalLightData(light);
            f32 intensity = IntensityOf(*light);
            Vector4 color = LinearColor(*light, intensity);
            const Matrix4x4& m = v.localToWorldMatrix;
            Vector4 forward = m.GetColumn(2);
            if (v.lightType == LightType::Directional)
            {
                f32 radius = (extra ? extra->angularDiameter() : 0.5f) * 0.5f * mathf::Deg2Rad;
                directionals.Push({Vector4(-forward.x, -forward.y, -forward.z, radius), color, color.x * 0.2126f + color.y * 0.7152f + color.z * 0.0722f, i32(i)});
                continue;
            }
            if ((v.lightType != LightType::Point && v.lightType != LightType::Spot) || i32(punctual.Size()) >= limit)
                continue;
            GpuPunctualLight g;
            Vector3 p = m.GetPosition();
            g.positionRange = Vector4(p.x, p.y, p.z, v.range);
            f32 radius = extra ? extra->shapeRadius() : 0.025f;
            g.colorRadius = Vector4(color.x, color.y, color.z, radius);
            bool spot = v.lightType == LightType::Spot;
            g.directionType = Vector4(forward.x, forward.y, forward.z, spot ? 1.0f : 0.0f);
            f32 cosOuter = std::cos(v.spotAngle * 0.5f * mathf::Deg2Rad);
            f32 cosInner = std::cos(light->innerSpotAngle() * 0.5f * mathf::Deg2Rad);
            f32 scale = spot ? 1.0f / Max(cosInner - cosOuter, 0.001f) : 0.0f;
            g.spot = Vector4(scale, spot ? -cosOuter * scale : 1.0f, extra && !extra->affectDiffuse() ? 0.0f : 1.0f, extra && !extra->affectSpecular() ? 0.0f : 1.0f);
            punctual.Push(g);
        }
        // The brightest directional light first: it has the shadows.
        std::stable_sort(directionals.begin(), directionals.end(), [](const Directional& a, const Directional& b) { return a.brightness > b.brightness; });
        frame_.directionalCount = Min(i32(directionals.Size()), kMaxDirectionalLights);
        frame_.mainLightIndex = frame_.directionalCount > 0 ? directionals[0].visibleIndex : -1;
        frame_.punctualCount = i32(punctual.Size());

        Vector4 dirs[kMaxDirectionalLights], colors[kMaxDirectionalLights];
        for (i32 i = 0; i < kMaxDirectionalLights; i++)
        {
            dirs[i] = i < frame_.directionalCount ? directionals[u32(i)].direction : Vector4(0, 1, 0, 0);
            colors[i] = i < frame_.directionalCount ? directionals[u32(i)].color : Vector4();
        }
        cmd.SetGlobalVectorArray(Shader::PropertyToID("_DirectionalLightDirections"), Span<const Vector4>(dirs, kMaxDirectionalLights));
        cmd.SetGlobalVectorArray(Shader::PropertyToID("_DirectionalLightColors"), Span<const Vector4>(colors, kMaxDirectionalLights));
        cmd.SetGlobalVector(Shader::PropertyToID("_PhysicalLightCounts"), Vector4(f32(frame_.directionalCount), f32(frame_.punctualCount), 0, 0));

        // The light buffer: its capacity the asset's limit (one entry at least), the frame's lights from its start.
        i32 capacity = Max(1, limit);
        if (!lights_ || capacity_ != capacity)
        {
            Release();
            lights_ = GraphicsBuffer::Create(GraphicsBufferTarget::Structured, 0, capacity, i32(sizeof(GpuPunctualLight)));
            if (lights_)
                lights_->SetName("_PunctualLights");
            capacity_ = capacity;
        }
        if (lights_ && !punctual.Empty())
            cmd.SetBufferData(lights_->Id(), punctual.Data(), u32(punctual.Size() * sizeof(GpuPunctualLight)), 0);
        cmd.SetGlobalBuffer(Shader::PropertyToID("_PunctualLights"), lights_);

        // The clusters: tiles of the asset's size, slices logarithmic in view depth from the near to the far plane.
        i32 tile = asset.clusterTileSize();
        frame_.clustersX = Max(1, (width + tile - 1) / tile);
        frame_.clustersY = Max(1, (height + tile - 1) / tile);
        frame_.clustersZ = asset.clusterDepthSlices();
        frame_.maxPerCluster = asset.maxLightsPerCluster();
        f32 n = Max(nearPlane, 0.001f), f = Max(farPlane, n * 1.001f);
        f32 sliceScale = f32(frame_.clustersZ) / std::log(f / n);
        cmd.SetGlobalVector(Shader::PropertyToID("_ClusterGrid"),
                            Vector4(f32(frame_.clustersX), f32(frame_.clustersY), f32(frame_.clustersZ), f32(frame_.maxPerCluster)));
        cmd.SetGlobalVector(Shader::PropertyToID("_ClusterParams"), Vector4(n, f, sliceScale, -std::log(n) * sliceScale));
        cmd.SetGlobalVector(Shader::PropertyToID("_ClusterScreen"), Vector4(f32(width), f32(height), 1.0f / f32(width), 1.0f / f32(height)));
        cmd.SetGlobalMatrix(Shader::PropertyToID("_ClusterView"), view);
        cmd.SetGlobalMatrix(Shader::PropertyToID("_ClusterProjection"), projection);
        cmd.SetGlobalMatrix(Shader::PropertyToID("_ClusterInverseProjection"), projection.inverse());
    }

    namespace
    {
        struct ClusterData
        {
            ComputeShader* shader = nullptr;
            i32 kernel = 0;
            BufferHandle lights, counts, indices;
            RenderGraph* graph = nullptr;
            i32 groups[3] = {1, 1, 1};
        };
    }

    void LightLoop::RecordClustering(RenderGraph& graph, ComputeShader* clustering)
    {
        counts_ = indices_ = BufferHandle();
        if (!clustering || !lights_)
            return;
        i32 clusters = frame_.clustersX * frame_.clustersY * frame_.clustersZ;
        BufferDesc countsDesc(clusters, 4);
        countsDesc.name = String("_ClusterLightCounts");
        BufferDesc indicesDesc(clusters * frame_.maxPerCluster, 4);
        indicesDesc.name = String("_ClusterLightIndices");
        counts_ = graph.CreateBuffer(countsDesc);
        indices_ = graph.CreateBuffer(indicesDesc);
        ClusterData* data = nullptr;
        PassBuilder<ClusterData> b = graph.AddComputePass("LightClustering", data);
        data->shader = clustering;
        data->kernel = clustering->KernelIndex("AssignLights");
        data->graph = &graph;
        data->lights = graph.ImportBuffer(lights_);
        data->counts = counts_;
        data->indices = indices_;
        // Groups of 4 x 4 x 4 clusters.
        data->groups[0] = (frame_.clustersX + 3) / 4;
        data->groups[1] = (frame_.clustersY + 3) / 4;
        data->groups[2] = (frame_.clustersZ + 3) / 4;
        b.UseBuffer(data->lights, AccessFlags::Read);
        b.UseBuffer(counts_, AccessFlags::WriteAll);
        b.UseBuffer(indices_, AccessFlags::WriteAll);
        b.SetRenderFunc([](ClusterData& d, ComputeGraphContext& ctx) {
            if (d.kernel < 0)
                return;
            static const i32 lightsId = Shader::PropertyToID("_PunctualLights"), countsId = Shader::PropertyToID("_ClusterLightCounts"),
                             indicesId = Shader::PropertyToID("_ClusterLightIndices");
            GraphicsBuffer* counts = d.graph->Resolve(d.counts);
            GraphicsBuffer* indices = d.graph->Resolve(d.indices);
            ctx.cmd->SetComputeBufferParam(d.shader, d.kernel, lightsId, d.graph->Resolve(d.lights));
            ctx.cmd->SetComputeBufferParam(d.shader, d.kernel, countsId, counts);
            ctx.cmd->SetComputeBufferParam(d.shader, d.kernel, indicesId, indices);
            ctx.cmd->DispatchCompute(d.shader, d.kernel, d.groups[0], d.groups[1], d.groups[2]);
            // The forward passes read them by name.
            ctx.cmd->SetGlobalBuffer(countsId, counts);
            ctx.cmd->SetGlobalBuffer(indicesId, indices);
        });
    }
}
