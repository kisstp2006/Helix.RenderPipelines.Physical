#pragma once

#include <Helix/Engine/Rendering/Camera.h>
#include <Helix/Engine/Rendering/Light.h>
#include <Helix/Engine/Script/GameBehaviour.h>
#include <Helix/RenderPipelines/Core/Volume/VolumeComponent.h>
#include <Helix/RenderPipelines/Physical/Settings/PhysicalSettings.h>

// The Physical pipeline's settings of a light and of a camera: components beside them.
namespace helix::rendering::physical
{
    // A light's intensity in a physical unit and its shape.
    HX_CLASS(Namespace = "HelixEngine.Rendering.Physical")
    class PhysicalAdditionalLightData : public engine::GameBehaviour
    {
    public:
        // The intensity in lightUnit: lux for directional lights; lumen, candela, nits or EV100 for point and spot lights.
        HX_PROPERTY() f32 intensity() const { return intensity_; }
        void SetIntensity(f32 value) { intensity_ = value < 0 ? 0 : value; }
        HX_PROPERTY() LightUnit lightUnit() const { return unit_; }
        // The intensity is kept: it reads in the new unit (SetLightUnitAndConvert converts it).
        void SetLightUnit(LightUnit value) { unit_ = value; }
        // The new unit with the intensity converted into it (no change when the units do not convert for the light).
        HX_FUNCTION() void SetLightUnitAndConvert(LightUnit value);
        // The radius of a point or spot light's sphere (metres): the intensity of nits and EV100, the light's smallest
        // distance in the falloff.
        HX_PROPERTY() f32 shapeRadius() const { return shapeRadius_; }
        void SetShapeRadius(f32 value) { shapeRadius_ = value < 0 ? 0 : value; }
        // The sun's angular diameter (degrees): widens a directional light's highlights.
        HX_PROPERTY() f32 angularDiameter() const { return angularDiameter_; }
        void SetAngularDiameter(f32 value) { angularDiameter_ = value < 0 ? 0 : value > 90 ? 90 : value; }
        // Whether the light reaches the diffuse and the specular lobes.
        HX_PROPERTY() bool affectDiffuse() const { return affectDiffuse_; }
        void SetAffectDiffuse(bool value) { affectDiffuse_ = value; }
        HX_PROPERTY() bool affectSpecular() const { return affectSpecular_; }
        void SetAffectSpecular(bool value) { affectSpecular_ = value; }

        // The intensity the shaders take (candela, lux for directional lights) for the light this is beside.
        f32 ShaderIntensity(engine::LightType type) const;

        HX_FUNCTION() void OnBeforeSerialize() {}
        HX_FUNCTION() void OnAfterDeserialize() {}

        // The serialized data version (for upgrades of older data).
        i32 version() const { return version_; }

    private:
        HX_FIELD(SerializeField, Name = "m_Intensity") f32 intensity_ = 600;
        HX_FIELD(SerializeField, Name = "m_LightUnit") LightUnit unit_ = LightUnit::Lumen;
        HX_FIELD(SerializeField, Name = "m_ShapeRadius") f32 shapeRadius_ = 0.025f;
        HX_FIELD(SerializeField, Name = "m_AngularDiameter") f32 angularDiameter_ = 0.5f;
        HX_FIELD(SerializeField, Name = "m_AffectDiffuse") bool affectDiffuse_ = true;
        HX_FIELD(SerializeField, Name = "m_AffectSpecular") bool affectSpecular_ = true;
        HX_FIELD(SerializeField, Name = "m_Version") i32 version_ = 1;
    };

    // A camera's antialiasing, its history and the volumes it blends.
    HX_CLASS(Namespace = "HelixEngine.Rendering.Physical")
    class PhysicalAdditionalCameraData : public engine::GameBehaviour
    {
    public:
        ~PhysicalAdditionalCameraData() override;

        HX_PROPERTY() AntialiasingMode antialiasing() const { return antialiasing_; }
        void SetAntialiasing(AntialiasingMode value) { antialiasing_ = value; }
        // The share of the history in each frame of TAA (0.5 to 0.97).
        HX_PROPERTY() f32 taaHistoryWeight() const { return taaHistoryWeight_; }
        void SetTaaHistoryWeight(f32 value) { taaHistoryWeight_ = value < 0.5f ? 0.5f : value > 0.97f ? 0.97f : value; }
        // The next frame starts its history (and the automatic exposure) anew: a cut.
        HX_FUNCTION() void ResetHistory() { resetHistory_ = true; }
        bool TakeResetHistory()
        {
            bool r = resetHistory_;
            resetHistory_ = false;
            return r;
        }
        HX_PROPERTY(Name = "volumeLayerMaskBits", Setter = "SetVolumeLayerMaskBits", Internal) i32 volumeLayerMaskBits() const { return i32(volumeLayerMask_.bits); }
        void SetVolumeLayerMaskBits(i32 value) { volumeLayerMask_.bits = u32(value); }
        // Where the volumes are blended for (null: the camera).
        HX_PROPERTY() engine::Transform* volumeTrigger() const { return volumeTrigger_.Get(); }
        void SetVolumeTrigger(engine::Transform* value) { volumeTrigger_ = engine::PPtr<engine::Transform>(value); }
        HX_PROPERTY() bool renderShadows() const { return renderShadows_; }
        void SetRenderShadows(bool value) { renderShadows_ = value; }

        HX_FUNCTION() void OnBeforeSerialize() {}
        HX_FUNCTION() void OnAfterDeserialize() {}

        // The serialized data version (for upgrades of older data).
        i32 version() const { return version_; }

    private:
        HX_FIELD(SerializeField, Name = "m_Antialiasing") AntialiasingMode antialiasing_ = AntialiasingMode::None;
        HX_FIELD(SerializeField, Name = "m_TaaHistoryWeight") f32 taaHistoryWeight_ = 0.9f;
        HX_FIELD(SerializeField, Name = "m_VolumeLayerMask") LayerMaskBits volumeLayerMask_{1};
        HX_FIELD(SerializeField, Name = "m_VolumeTrigger") engine::PPtr<engine::Transform> volumeTrigger_;
        HX_FIELD(SerializeField, Name = "m_RenderShadows") bool renderShadows_ = true;
        HX_FIELD(SerializeField, Name = "m_Version") i32 version_ = 1;
        bool resetHistory_ = false;
    };

    // The component beside a light or a camera (added when there is none: a light's takes the light's intensity in its
    // type's default unit).
    PhysicalAdditionalLightData* GetPhysicalAdditionalLightData(engine::Light* light);
    PhysicalAdditionalCameraData* GetPhysicalAdditionalCameraData(engine::Camera* camera);
    // The component when there is one (null else).
    PhysicalAdditionalLightData* FindPhysicalAdditionalLightData(const engine::Light* light);
    PhysicalAdditionalCameraData* FindPhysicalAdditionalCameraData(const engine::Camera* camera);

    // What C#'s extension methods call.
    HX_CLASS(Namespace = "HelixEngine.Rendering.Physical", Static, Internal)
    class PhysicalAdditionalDataNative
    {
    public:
        HX_FUNCTION() static PhysicalAdditionalCameraData* OfCamera(engine::Camera* camera) { return GetPhysicalAdditionalCameraData(camera); }
        HX_FUNCTION() static PhysicalAdditionalLightData* OfLight(engine::Light* light) { return GetPhysicalAdditionalLightData(light); }
    };
}
