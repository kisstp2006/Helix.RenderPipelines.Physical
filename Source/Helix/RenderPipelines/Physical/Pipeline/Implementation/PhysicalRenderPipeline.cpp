#include <Helix/RenderPipelines/Physical/Pipeline/PhysicalRenderPipeline.h>

#include <Helix/Core/Logging/Log.h>
#include <Helix/Engine/GameObject/GameObject.h>
#include <Helix/Engine/Rendering/GL.h>
#include <Helix/Engine/Rendering/Graphics.h>
#include <Helix/Engine/Rendering/RenderSettings.h>
#include <Helix/Engine/Time/Time.h>
#include <Helix/RHI/Device/Device.h>
#include <Helix/RenderPipelines/Core/Shadows/CascadedShadows.h>
#include <Helix/RenderPipelines/Core/Textures/RTHandle.h>
#include <Helix/RenderPipelines/Core/Utilities/Blitter.h>
#include <Helix/RenderPipelines/Core/Volume/Volume.h>
#include <Helix/RenderPipelines/Physical/Components/AdditionalData.h>
#include <Helix/RenderPipelines/Physical/Energy/EnergyTables.inl>
#include <Helix/RenderPipelines/Physical/Energy/EnergyIntegration.h>
#include <Helix/RenderPipelines/Physical/Lighting/LightUnits.h>
#include <Helix/Scripting/Bindings/Interop.h>

#include <cmath>

namespace helix::rendering::physical
{
    using namespace engine;

    namespace
    {
        LogChannel gPhysicalLog{"Physical"};

        PhysicalFrameInfo& Last()
        {
            static PhysicalFrameInfo info;
            return info;
        }

        // The pipeline the engine renders with (null without one).
        PhysicalRenderPipeline* gActive = nullptr;

        // The defaults of the volume components when no volume sets them.
        const Exposure& DefaultExposure()
        {
            static Exposure e;
            return e;
        }

        const Tonemapping& DefaultTonemapping()
        {
            static Tonemapping t;
            return t;
        }

        Array<ShaderTagId> PassNames(std::initializer_list<const char*> names)
        {
            Array<ShaderTagId> out;
            for (const char* n : names)
                out.Push(ShaderTagId(n));
            return out;
        }

        struct DrawData
        {
            RendererListHandle list;
            RenderGraph* graph = nullptr;
        };

        struct FullscreenData
        {
            Material* material = nullptr;
            RenderGraph* graph = nullptr;
            TextureHandle source;
            MaterialPropertyBlock block;
        };

        constexpr GraphicsFormat kColorFormat = GraphicsFormat::R16G16B16A16_SFloat;
    }

    // ---------------------------------------------------------------------------------------------- the asset

    PhysicalRenderPipelineAsset::~PhysicalRenderPipelineAsset()
    {
        if (Material* m = defaultMaterial_.Get())
            Object::DestroyImmediate(m);
    }

    PhysicalRenderPipelineAsset* PhysicalRenderPipelineAsset::Create()
    {
        return static_cast<PhysicalRenderPipelineAsset*>(ScriptableObject::CreateInstance(scripting::TypeOf<PhysicalRenderPipelineAsset>()));
    }

    void PhysicalRenderPipelineAsset::SetMaxVisibleLights(i32 value) { maxVisibleLights_ = value < 1 ? 1 : value > 1024 ? 1024 : value; }
    void PhysicalRenderPipelineAsset::SetMaxLightsPerCluster(i32 value) { maxLightsPerCluster_ = value < 4 ? 4 : value > 128 ? 128 : value; }
    void PhysicalRenderPipelineAsset::SetClusterTileSize(i32 value) { clusterTileSize_ = value < 8 ? 8 : value > 256 ? 256 : value; }
    void PhysicalRenderPipelineAsset::SetClusterDepthSlices(i32 value) { clusterDepthSlices_ = value < 4 ? 4 : value > 64 ? 64 : value; }

    void PhysicalRenderPipelineAsset::SetShadowmapResolution(i32 value)
    {
        i32 r = 512;
        while (r < value && r < 4096)
            r *= 2;
        shadowResolution_ = ShadowResolution(r);
    }

    Shader* PhysicalRenderPipelineAsset::defaultShader() const { return Shader::Find("Physical/OpenPBR Lite"); }

    Material* PhysicalRenderPipelineAsset::defaultMaterial()
    {
        if (!defaultMaterial_.Get())
            if (Shader* shader = defaultShader())
            {
                Material* m = Material::Create(shader);
                m->SetName("OpenPBR Lite");
                defaultMaterial_ = PPtr<Material>(m);
            }
        return defaultMaterial_.Get();
    }

    RenderPipeline* PhysicalRenderPipelineAsset::CreatePipeline() { return new PhysicalRenderPipeline(this); }

    // ---------------------------------------------------------------------------------------------- the pipeline

    PhysicalRenderPipelineAsset* PhysicalRenderPipeline::asset()
    {
        ScriptableObject* current = GraphicsSettings::currentRenderPipeline();
        if (!current || !scripting::IsAssignableFrom(scripting::TypeOf<PhysicalRenderPipelineAsset>(), current->Class().type))
            return nullptr;
        return static_cast<PhysicalRenderPipelineAsset*>(current);
    }

    bool PhysicalRenderPipeline::SupportsDevice(String* reason)
    {
        rhi::Device* device = Graphics::Device();
        if (!device)
        {
            if (reason)
                *reason = String("there is no graphics device");
            return false;
        }
        const rhi::DeviceCaps& c = device->Caps();
        bool api = c.backend == rhi::Backend::D3D11 || (c.backend == rhi::Backend::OpenGL && (c.apiMajor > 4 || (c.apiMajor == 4 && c.apiMinor >= 5)));
        if (api && c.compute && c.storageBuffers)
            return true;
        if (reason)
            *reason = String("it needs compute shaders and storage buffers in every stage (D3D11 feature level 11 or OpenGL 4.5)");
        return false;
    }

    bool PhysicalRenderPipeline::UsesBindless(const PhysicalRenderPipelineAsset* asset)
    {
        rhi::Device* device = Graphics::Device();
        return asset && asset->bindlessMaterials() && device && device->Caps().bindlessTextures && device->Caps().backend == rhi::Backend::OpenGL;
    }

    const PhysicalFrameInfo& PhysicalRenderPipeline::LastFrame() { return Last(); }

    bool PhysicalRenderPipeline::ReadExposure(const Camera* camera, f32& exposure, f32& ev100)
    {
        if (!gActive || !camera)
            return false;
        std::unique_ptr<CameraState>* s = gActive->cameras_.Find(camera->GetEntityId().value);
        if (!s || !(*s)->exposure.buffer)
            return false;
        f32 values[4] = {};
        if (!(*s)->exposure.buffer->GetData(values, sizeof(values), 0))
            return false;
        exposure = values[0];
        ev100 = values[1];
        return true;
    }

    PhysicalRenderPipeline::PhysicalRenderPipeline(PhysicalRenderPipelineAsset* asset) : asset_(asset), graph_("Physical")
    {
        Blitter::Initialize(Shader::Find("Hidden/Core/Blit"), Shader::Find("Hidden/Core/BlitColorAndDepth"));
        VolumeManager& volumes = VolumeManager::instance();
        if (!volumes.isInitialized())
            volumes.Initialize(asset ? asset->volumeProfile() : nullptr, nullptr);
        GraphicsSettings::SetUseScriptableRenderPipelineBatching(true);
        gActive = this;
    }

    PhysicalRenderPipeline::~PhysicalRenderPipeline()
    {
        if (gActive == this)
            gActive = nullptr;
    }

    void PhysicalRenderPipeline::Dispose(bool disposing)
    {
        (void)disposing;
        cameras_.Clear();
        lights_.Release();
        graph_.Cleanup();
        for (Object* o : {static_cast<Object*>(energyTable_.Get()), static_cast<Object*>(energyAverage_.Get()), static_cast<Object*>(sky_.Get()),
                          static_cast<Object*>(temporal_.Get()), static_cast<Object*>(final_.Get())})
            if (o)
                Object::DestroyImmediate(o);
        energyTable_ = PPtr<Texture2D>();
        energyAverage_ = PPtr<Texture2D>();
        sky_ = temporal_ = final_ = PPtr<Material>();
        Shader::DisableKeyword("_PHYSICAL_BINDLESS");
        VolumeManager& volumes = VolumeManager::instance();
        if (volumes.isInitialized())
            volumes.Deinitialize();
    }

    void PhysicalRenderPipeline::EnsureResources()
    {
        if (!energyTable_.Get())
        {
            // The tables of Tools/EnergyTables: per roughness row the GGX lobe's A, B, C and the FON lobe's albedo.
            Texture2D* t = Texture2D::Create(energy::kSize, energy::kSize, TextureFormat::RGBAFloat, false, true);
            t->SetName("_PhysicalEnergyTable");
            t->hideFlags = HideFlags::HideAndDontSave;
            t->SetPixelData(Span<const f32>(energy::kTable, usize(energy::kSize) * energy::kSize * 4), 0);
            t->SetFilterMode(FilterMode::Bilinear);
            t->SetWrapMode(TextureWrapMode::Clamp);
            t->Apply(false);
            energyTable_ = PPtr<Texture2D>(t);
            Texture2D* a = Texture2D::Create(energy::kSize, 1, TextureFormat::RGBAFloat, false, true);
            a->SetName("_PhysicalEnergyAverage");
            a->hideFlags = HideFlags::HideAndDontSave;
            Array<f32> avg;
            for (i32 j = 0; j < energy::kSize; j++)
                for (f32 v : {energy::kAverage[j * 2], energy::kAverage[j * 2 + 1], 0.0f, 0.0f})
                    avg.Push(v);
            a->SetPixelData(Span<const f32>(avg.Data(), avg.Size()), 0);
            a->SetFilterMode(FilterMode::Bilinear);
            a->SetWrapMode(TextureWrapMode::Clamp);
            a->Apply(false);
            energyAverage_ = PPtr<Texture2D>(a);
        }
        auto make = [](PPtr<Material>& m, const char* shader) {
            if (!m.Get())
                if (Shader* s = Shader::Find(shader))
                {
                    Material* mat = Material::Create(s);
                    mat->hideFlags = HideFlags::HideAndDontSave;
                    m = PPtr<Material>(mat);
                }
        };
        make(sky_, "Hidden/Physical/Sky");
        make(temporal_, "Hidden/Physical/TemporalAA");
        make(final_, "Hidden/Physical/Final");
    }

    PhysicalRenderPipeline::CameraState& PhysicalRenderPipeline::StateOf(Camera* camera)
    {
        u64 key = camera->GetEntityId().value;
        if (std::unique_ptr<CameraState>* s = cameras_.Find(key))
            return **s;
        return *cameras_.Set(key, std::make_unique<CameraState>());
    }

    void PhysicalRenderPipeline::Render(ScriptableRenderContext& context, Span<Camera* const> cameras)
    {
        BeginContextRendering(context, cameras);
        String reason;
        if (!SupportsDevice(&reason))
        {
            // Nothing is drawn on other devices (once per pipeline).
            if (!errorLogged_)
                HX_LOG_ERROR(gPhysicalLog, "The Physical pipeline does not draw on this graphics device: {}.", reason);
            errorLogged_ = true;
            EndContextRendering(context, cameras);
            return;
        }
        EnsureResources();
        for (Camera* camera : cameras)
            if (camera)
                RenderCamera(context, camera);
        graph_.EndFrame();
        EndContextRendering(context, cameras);
    }

    void PhysicalRenderPipeline::RenderCamera(ScriptableRenderContext& context, Camera* camera)
    {
        BeginCameraRendering(context, camera);
        PhysicalRenderPipelineAsset* asset = asset_.Get();
        ScriptableCullingParameters parameters;
        if (!asset || !camera->TryGetCullingParameters(parameters))
        {
            EndCameraRendering(context, camera);
            return;
        }
        PhysicalAdditionalCameraData* extra = FindPhysicalAdditionalCameraData(camera);
        CameraState& state = StateOf(camera);
        i32 width = Max(1, camera->pixelWidth()), height = Max(1, camera->pixelHeight());
        bool shadows = asset->supportsShadows() && (!extra || extra->renderShadows());
        f32 shadowDistance = shadows ? Min(asset->shadowDistance(), camera->farClipPlane()) : 0.0f;
        parameters.shadowDistance = shadowDistance;
        CullingResults culling = context.Cull(parameters);
        context.SetupCameraProperties(camera);

        // A cut: asked for, the first frame, or the camera jumped far.
        Vector3 position = camera->transform() ? camera->transform()->position() : Vector3();
        bool reset = (extra && extra->TakeResetHistory()) || !state.seen || (position - state.lastPosition).magnitude() > 100.0f;
        state.seen = true;
        state.lastPosition = position;

        // The camera's volumes.
        Transform* trigger = extra && extra->volumeTrigger() ? extra->volumeTrigger() : camera->transform();
        VolumeManager& volumes = VolumeManager::instance();
        volumes.Update(trigger, LayerMask(extra ? u32(extra->volumeLayerMaskBits()) : 1u));
        VolumeStack* stack = volumes.stack();
        const Exposure* exposureVolume = stack ? stack->GetComponent<Exposure>() : nullptr;
        const Tonemapping* tonemapVolume = stack ? stack->GetComponent<Tonemapping>() : nullptr;
        const Exposure& exposure = exposureVolume ? *exposureVolume : DefaultExposure();
        const Tonemapping& tonemap = tonemapVolume ? *tonemapVolume : DefaultTonemapping();

        CommandBuffer cmd;
        cmd.SetName("Physical");
        bool bindless = UsesBindless(asset);
        if (bindless)
            cmd.EnableShaderKeyword("_PHYSICAL_BINDLESS");
        else
            cmd.DisableShaderKeyword("_PHYSICAL_BINDLESS");

        // The projection, jittered for TAA; the unjittered one for the clusters, the sky and the reprojection.
        bool taa = extra && extra->antialiasing() == AntialiasingMode::TemporalAntialiasing && temporal_.Get() &&
                   TemporalAA::Prepare(state.temporal, width, height, kColorFormat);
        Matrix4x4 view = camera->worldToCameraMatrix();
        Matrix4x4 projection = camera->projectionMatrix();
        Vector2 jitter = taa ? TemporalAA::JitterOffset(state.temporal.frame) : Vector2(0, 0);
        Matrix4x4 jittered = taa ? TemporalAA::JitterProjection(projection, jitter, width, height) : projection;
        if (taa)
            cmd.SetViewProjectionMatrices(view, jittered);

        // The exposure, the lights, the environment, the energy tables.
        ExposureControl::Setup(cmd, state.exposure, exposure, *camera, reset);
        lights_.Setup(cmd, culling, *asset, width, height, view, projection, camera->nearClipPlane(), camera->farClipPlane());
        const LightLoopFrame& lightFrame = lights_.frame();
        f32 environment = asset->environmentLuminance();
        cmd.SetGlobalVector(Shader::PropertyToID("_PhysicalEnvironment"),
                            Vector4(environment, RenderSettings::ambientIntensity(), RenderSettings::reflectionIntensity(), 0));
        cmd.SetGlobalTexture(Shader::PropertyToID("_PhysicalEnergyTable"), energyTable_.Get());
        cmd.SetGlobalTexture(Shader::PropertyToID("_PhysicalEnergyAverage"), energyAverage_.Get());

        // The sun's shadows: the brightest directional light's, with the core's cascades.
        Light* mainLight = lightFrame.mainLightIndex >= 0 ? culling.visibleLights()[u32(lightFrame.mainLightIndex)].light() : nullptr;
        Bounds casters;
        bool sunShadows = shadows && shadowDistance > 0 && mainLight && mainLight->shadows() != LightShadows::None &&
                          culling.GetShadowCasterBounds(lightFrame.mainLightIndex, casters);
        cmd.SetGlobalVector(Shader::PropertyToID("_PhysicalLightCounts"),
                            Vector4(f32(lightFrame.directionalCount), f32(lightFrame.punctualCount), sunShadows ? 1.0f : 0.0f, 0));

        RenderGraphParameters graphParameters;
        graphParameters.commandBuffer = &cmd;
        graphParameters.scriptableRenderContext = &context;
        graphParameters.executionName = String(camera->Name());
        RTHandles::SetReferenceSize(width, height);
        if (!graph_.BeginRecording(graphParameters))
        {
            EndCameraRendering(context, camera);
            return;
        }

        if (sunShadows)
        {
            CascadedShadowSettings s;
            s.lightIndex = lightFrame.mainLightIndex;
            s.cascadeCount = asset->shadowCascadeCount();
            Vector3 splits = asset->cascadeSplits();
            s.splits = s.cascadeCount == 2 ? Vector3(splits.x, 1, 0) : s.cascadeCount == 3 ? Vector3(splits.x, splits.y, 0) : s.cascadeCount == 4 ? splits : Vector3(1, 0, 0);
            s.atlasWidth = asset->shadowmapResolution();
            s.atlasHeight = s.cascadeCount == 2 ? s.atlasWidth / 2 : s.atlasWidth;
            s.depthBits = 32;
            s.bias = Vector2(asset->shadowDepthBias(), asset->shadowNormalBias());
            s.soft = asset->softShadows() && mainLight->shadows() == LightShadows::Soft;
            s.softQuality = s.soft ? 2.0f : 0.0f;
            s.strength = mainLight->shadowStrength();
            s.shadowDistance = shadowDistance;
            s.cascadeBorder = asset->cascadeBorder();
            s.nearPlane = mainLight->shadowNearPlane();
            sunShadows = CascadedShadows::Record(graph_, context, culling, s, view, taa ? jittered : projection, cmd).IsValid();
        }
        if (!sunShadows)
            cmd.SetGlobalVector(Shader::PropertyToID("_MainLightShadowParams"), Vector4(0, 0, 0, 0));

        // The camera's color (pre-exposed HDR), depth and normals.
        RenderTargetInfo info;
        info.width = width;
        info.height = height;
        ImportResourceParams backImport;
        TextureHandle backbuffer = graph_.ImportBackbuffer(RenderTargetIdentifier(BuiltinRenderTextureType::CameraTarget), info, backImport);
        TextureDesc colorDesc(width, height);
        colorDesc.colorFormat = kColorFormat;
        colorDesc.clearBuffer = true;
        colorDesc.clearColor = Color(0, 0, 0, 0);
        colorDesc.filterMode = FilterMode::Bilinear;
        colorDesc.name = String("_CameraColorAttachment");
        TextureHandle color = graph_.CreateTexture(colorDesc);
        TextureDesc depthDesc(width, height);
        depthDesc.colorFormat = GraphicsFormat::None;
        depthDesc.depthBufferBits = DepthBits::Depth32;
        depthDesc.clearBuffer = true;
        depthDesc.name = String("_CameraDepthAttachment");
        TextureHandle depth = graph_.CreateTexture(depthDesc);
        TextureDesc normalDesc(width, height);
        normalDesc.colorFormat = GraphicsFormat::R16G16B16A16_SFloat;
        normalDesc.clearBuffer = true;
        normalDesc.name = String("_CameraNormalsTexture");
        TextureHandle normals = graph_.CreateTexture(normalDesc);

        auto listDesc = [&](bool opaque, Array<ShaderTagId> names) {
            RendererUtils::RendererListDesc desc;
            desc.passNames = Move(names);
            desc.cullingResults = culling;
            desc.camera = camera;
            desc.sortingCriteria = opaque ? SortingCriteria::CommonOpaque : SortingCriteria::CommonTransparent;
            desc.renderQueueRange = opaque ? RenderQueueRange::opaque() : RenderQueueRange::transparent();
            desc.rendererConfiguration = PerObjectData(i32(PerObjectData::ReflectionProbes) | i32(PerObjectData::LightProbe));
            return desc;
        };
        auto draw = [&](StringView name, RendererListHandle list, TextureHandle target, AccessFlags depthAccess, bool readsClusters) {
            DrawData* data = nullptr;
            PassBuilder<DrawData> b = graph_.AddRasterRenderPass(name, data);
            data->list = list;
            data->graph = &graph_;
            b.UseRendererList(list);
            b.SetRenderAttachment(target, 0);
            b.SetRenderAttachmentDepth(depth, depthAccess);
            if (readsClusters && lights_.counts().IsValid())
            {
                b.UseBuffer(lights_.counts(), AccessFlags::Read);
                b.UseBuffer(lights_.indices(), AccessFlags::Read);
            }
            b.AllowGlobalStateModification(true);
            b.UseAllGlobalTextures(true);
            b.SetRenderFunc([](DrawData& d, RasterGraphContext& ctx) { ctx.cmd->DrawRendererList(d.graph->ResolveList(d.list)); });
        };

        // The depth and normal prepass, the clusters, the opaque surfaces (on the prepass's depth).
        draw("DepthNormals", graph_.CreateRendererList(listDesc(true, PassNames({"PhysicalDepthNormals"}))), normals, AccessFlags::Write, false);
        lights_.RecordClustering(graph_, ComputeShader::Find("Hidden/Physical/LightClustering"));
        draw("DrawOpaqueObjects", graph_.CreateRendererList(listDesc(true, PassNames({"PhysicalForward", "SRPDefaultUnlit"}))), color, AccessFlags::Write, true);

        // The sky behind them (the skybox's cube, else the background color), in the environment's luminance.
        CameraClearFlags clear = camera->clearFlags();
        if (sky_.Get() && (clear == CameraClearFlags::Skybox || clear == CameraClearFlags::SolidColor))
        {
            FullscreenData* data = nullptr;
            PassBuilder<FullscreenData> b = graph_.AddRasterRenderPass("Sky", data);
            data->material = sky_.Get();
            data->graph = &graph_;
            Material* skybox = RenderSettings::skybox();
            Texture* cube = clear == CameraClearFlags::Skybox && skybox && skybox->HasProperty("_Tex") ? skybox->GetTexture("_Tex") : nullptr;
            Color tint = skybox && skybox->HasProperty("_Tint") ? skybox->GetColor("_Tint") : Color(0.5f, 0.5f, 0.5f, 1);
            f32 skyExposure = skybox && skybox->HasProperty("_Exposure") ? skybox->GetFloat("_Exposure") : 1.0f;
            f32 rotation = skybox && skybox->HasProperty("_Rotation") ? skybox->GetFloat("_Rotation") : 0.0f;
            Color background = clear == CameraClearFlags::SolidColor ? camera->backgroundColor() : RenderSettings::ambientSkyColor();
            Transform* t = camera->transform();
            Vector3 right = t ? t->right() : Vector3(1, 0, 0), up = t ? t->up() : Vector3(0, 1, 0), forward = t ? t->forward() : Vector3(0, 0, 1);
            MaterialPropertyBlock& block = data->block;
            if (cube)
                block.SetTexture("_SkyCube", cube);
            // The tint of the reference's skybox doubles at 0.5 (its grey); colors are sRGB.
            block.SetVector("_SkyTint", Vector4(mathf::GammaToLinearSpace(tint.r) * 2.0f, mathf::GammaToLinearSpace(tint.g) * 2.0f,
                                                mathf::GammaToLinearSpace(tint.b) * 2.0f, 1));
            block.SetVector("_SkyColor", Vector4(mathf::GammaToLinearSpace(background.r), mathf::GammaToLinearSpace(background.g),
                                                 mathf::GammaToLinearSpace(background.b), 1));
            block.SetVector("_SkyParams", Vector4(cube ? 1.0f : 0.0f, rotation * mathf::Deg2Rad, skyExposure, environment));
            const Matrix4x4& p = taa ? jittered : projection;
            block.SetVector("_SkyProjection", Vector4(p.m00, p.m11, p.m02, p.m12));
            block.SetVector("_SkyRight", Vector4(right.x, right.y, right.z, 0));
            block.SetVector("_SkyUp", Vector4(up.x, up.y, up.z, 0));
            block.SetVector("_SkyForward", Vector4(forward.x, forward.y, forward.z, 0));
            b.SetRenderAttachment(color, 0);
            b.SetRenderAttachmentDepth(depth, AccessFlags::Read);
            b.SetRenderFunc([](FullscreenData& d, RasterGraphContext& ctx) {
                ctx.cmd->DrawProcedural(Matrix4x4::identity, d.material, 0, MeshTopology::Triangles, 3, 1, &d.block);
            });
        }

        draw("DrawTransparentObjects", graph_.CreateRendererList(listDesc(false, PassNames({"PhysicalForward", "SRPDefaultUnlit"}))), color, AccessFlags::Read,
             true);

        // The automatic exposure from this frame, TAA, the tonemapped result in the target.
        ExposureControl::RecordAutomatic(graph_, ComputeShader::Find("Hidden/Physical/Exposure"), state.exposure, color, exposure, width, height,
                                         Time::deltaTime(), reset);
        TextureHandle resolved = color;
        Matrix4x4 viewProjection = GL::GetGPUProjectionMatrix(projection, false) * view;
        if (taa)
            resolved = TemporalAA::Record(graph_, temporal_.Get(), state.temporal, color, depth, viewProjection.inverse(), extra->taaHistoryWeight(), jitter, reset);
        if (final_.Get())
        {
            FullscreenData* data = nullptr;
            PassBuilder<FullscreenData> b = graph_.AddRasterRenderPass("Final", data);
            data->material = final_.Get();
            data->graph = &graph_;
            data->source = resolved;
            data->block.SetVector("_FinalParams", Vector4(f32(i32(tonemap.mode.value())), 0, 0, 0));
            b.UseTexture(resolved, AccessFlags::Read);
            b.SetRenderAttachment(backbuffer, 0, AccessFlags::WriteAll);
            b.SetRenderFunc([](FullscreenData& d, RasterGraphContext& ctx) {
                RTHandle* source = d.graph->Resolve(d.source);
                if (!source)
                    return;
                d.block.SetTexture("_FinalSource", source->rt());
                ctx.cmd->DrawProcedural(Matrix4x4::identity, d.material, 0, MeshTopology::Triangles, 3, 1, &d.block);
            });
        }

        graph_.EndRecordingAndExecute();
        if (taa)
            TemporalAA::Finish(state.temporal, viewProjection);
        context.ExecuteCommandBuffer(cmd);
        context.Submit();

        PhysicalFrameInfo& last = Last();
        last.punctualLights = lightFrame.punctualCount;
        last.directionalLights = lightFrame.directionalCount;
        last.clustersX = lightFrame.clustersX;
        last.clustersY = lightFrame.clustersY;
        last.clustersZ = lightFrame.clustersZ;
        last.shadows = sunShadows;
        last.temporalAntialiasing = taa;
        last.bindless = bindless;
        last.ev100 = state.exposure.ev100;
        state.frame++;
        EndCameraRendering(context, camera);
    }
}
