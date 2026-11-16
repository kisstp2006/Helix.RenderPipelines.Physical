#pragma once

#include <Helix/Engine/Material/Material.h>
#include <Helix/Engine/Rendering/Pipeline/RenderPipeline.h>
#include <Helix/Engine/Script/ScriptableObject.h>
#include <Helix/Engine/Shader/Shader.h>
#include <Helix/RenderPipelines/Core/Volume/VolumeComponent.h>
#include <Helix/RenderPipelines/Physical/Settings/PhysicalSettings.h>

// The Physical pipeline's asset: its light clustering, shadow, environment and material settings, set in
// GraphicsSettings or QualitySettings; the engine makes its pipeline in C++. Its C# class derives from
// RenderPipelineAsset.
namespace helix::rendering::physical
{
    HX_CLASS(Namespace = "HelixEngine.Rendering.Physical", CSharpBase = "HelixEngine.Rendering.RenderPipelineAsset")
    class PhysicalRenderPipelineAsset : public engine::ScriptableObject
    {
    public:
        static constexpr i32 k_AssetVersion = 1;

        ~PhysicalRenderPipelineAsset() override;

        HX_FUNCTION() static PhysicalRenderPipelineAsset* Create();

        // ------------------------------------------------------------------------------------------ lights
        // The point and spot lights a camera draws at most (the brightest by distance first); 1 to 1024.
        HX_PROPERTY() i32 maxVisibleLights() const { return maxVisibleLights_; }
        void SetMaxVisibleLights(i32 value);
        // The lights one cluster holds at most; 4 to 128.
        HX_PROPERTY() i32 maxLightsPerCluster() const { return maxLightsPerCluster_; }
        void SetMaxLightsPerCluster(i32 value);
        // The clusters' screen tiles in pixels (8 to 256) and their slices in depth (logarithmic, 4 to 64).
        HX_PROPERTY() i32 clusterTileSize() const { return clusterTileSize_; }
        void SetClusterTileSize(i32 value);
        HX_PROPERTY() i32 clusterDepthSlices() const { return clusterDepthSlices_; }
        void SetClusterDepthSlices(i32 value);
        // The luminance (nits) of a value of 1 of the environment: the skybox, the ambient probe and the reflections.
        HX_PROPERTY() f32 environmentLuminance() const { return environmentLuminance_; }
        void SetEnvironmentLuminance(f32 value) { environmentLuminance_ = value < 0 ? 0 : value; }

        // ------------------------------------------------------------------------------------------ shadows
        HX_PROPERTY() bool supportsShadows() const { return supportsShadows_; }
        void SetSupportsShadows(bool value) { supportsShadows_ = value; }
        HX_PROPERTY() i32 shadowmapResolution() const { return i32(shadowResolution_); }
        void SetShadowmapResolution(i32 value);
        HX_PROPERTY() f32 shadowDistance() const { return shadowDistance_; }
        void SetShadowDistance(f32 value) { shadowDistance_ = value < 0 ? 0 : value; }
        // 1 to 4.
        HX_PROPERTY() i32 shadowCascadeCount() const { return shadowCascadeCount_; }
        void SetShadowCascadeCount(i32 value) { shadowCascadeCount_ = value < 1 ? 1 : value > 4 ? 4 : value; }
        // The ends of the first three cascades as shares of the shadow distance (the splits of the cascade count).
        HX_PROPERTY() Vector3 cascadeSplits() const { return cascadeSplits_; }
        void SetCascadeSplits(Vector3 value) { cascadeSplits_ = value; }
        // The share of the last cascade over which the shadow fades out.
        HX_PROPERTY() f32 cascadeBorder() const { return cascadeBorder_; }
        void SetCascadeBorder(f32 value) { cascadeBorder_ = value; }
        HX_PROPERTY() f32 shadowDepthBias() const { return shadowDepthBias_; }
        void SetShadowDepthBias(f32 value) { shadowDepthBias_ = value < 0 ? 0 : value > 10 ? 10 : value; }
        HX_PROPERTY() f32 shadowNormalBias() const { return shadowNormalBias_; }
        void SetShadowNormalBias(f32 value) { shadowNormalBias_ = value < 0 ? 0 : value > 10 ? 10 : value; }
        // Soft shadows: the 5x5 tent filter (else one hard tap).
        HX_PROPERTY() bool softShadows() const { return softShadows_; }
        void SetSoftShadows(bool value) { softShadows_ = value; }

        // ------------------------------------------------------------------------------------------ materials
        // The OpenPBR materials' textures through the device's bindless tables where it has them.
        HX_PROPERTY() bool bindlessMaterials() const { return bindlessMaterials_; }
        void SetBindlessMaterials(bool value) { bindlessMaterials_ = value; }

        // ------------------------------------------------------------------------------------------ volumes
        // The default volume profile (the values volumes in the scene override).
        HX_PROPERTY() VolumeProfile* volumeProfile() const { return volumeProfile_.Get(); }
        void SetVolumeProfile(VolumeProfile* value) { volumeProfile_ = engine::PPtr<VolumeProfile>(value); }

        // ------------------------------------------------------------------------------------------ the engine's
        // Physical/OpenPBR Lite and a material of it (null until the pipeline's shaders load).
        HX_PROPERTY(Name = "defaultShaderNative", Internal) engine::Shader* defaultShader() const;
        HX_PROPERTY(Name = "defaultMaterialNative", Internal) engine::Material* defaultMaterial();
        HX_PROPERTY(ReadOnly) static String packagePath() { return String("Packages/Helix.RenderPipelines.Physical"); }

        engine::RenderPipeline* CreatePipeline();

        HX_FUNCTION() void OnBeforeSerialize() {}
        HX_FUNCTION() void OnAfterDeserialize() {}

        // The serialized asset version (for upgrades of older assets).
        i32 assetVersion() const { return assetVersion_; }

    private:
        HX_FIELD(SerializeField, Name = "k_AssetVersion") i32 assetVersion_ = k_AssetVersion;
        HX_FIELD(SerializeField, Name = "m_MaxVisibleLights") i32 maxVisibleLights_ = 256;
        HX_FIELD(SerializeField, Name = "m_MaxLightsPerCluster") i32 maxLightsPerCluster_ = 32;
        HX_FIELD(SerializeField, Name = "m_ClusterTileSize") i32 clusterTileSize_ = 64;
        HX_FIELD(SerializeField, Name = "m_ClusterDepthSlices") i32 clusterDepthSlices_ = 16;
        HX_FIELD(SerializeField, Name = "m_EnvironmentLuminance") f32 environmentLuminance_ = 5000;
        HX_FIELD(SerializeField, Name = "m_SupportsShadows") bool supportsShadows_ = true;
        HX_FIELD(SerializeField, Name = "m_ShadowmapResolution") ShadowResolution shadowResolution_ = ShadowResolution::_2048;
        HX_FIELD(SerializeField, Name = "m_ShadowDistance") f32 shadowDistance_ = 100;
        HX_FIELD(SerializeField, Name = "m_ShadowCascadeCount") i32 shadowCascadeCount_ = 4;
        HX_FIELD(SerializeField, Name = "m_CascadeSplits") Vector3 cascadeSplits_{0.067f, 0.2f, 0.467f};
        HX_FIELD(SerializeField, Name = "m_CascadeBorder") f32 cascadeBorder_ = 0.2f;
        HX_FIELD(SerializeField, Name = "m_ShadowDepthBias") f32 shadowDepthBias_ = 1;
        HX_FIELD(SerializeField, Name = "m_ShadowNormalBias") f32 shadowNormalBias_ = 1;
        HX_FIELD(SerializeField, Name = "m_SoftShadows") bool softShadows_ = true;
        HX_FIELD(SerializeField, Name = "m_BindlessMaterials") bool bindlessMaterials_ = true;
        HX_FIELD(SerializeField, Name = "m_VolumeProfile") engine::PPtr<VolumeProfile> volumeProfile_;
        engine::PPtr<engine::Material> defaultMaterial_;
    };
}
