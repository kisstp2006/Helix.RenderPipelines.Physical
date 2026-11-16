#include <Helix/RenderPipelines/Physical/Components/AdditionalData.h>

#include <Helix/Engine/GameObject/GameObject.h>
#include <Helix/RenderPipelines/Physical/Lighting/LightUnits.h>

namespace helix::rendering::physical
{
    void PhysicalAdditionalLightData::SetLightUnitAndConvert(LightUnit value)
    {
        engine::Light* light = gameObject() ? static_cast<engine::Light*>(gameObject()->GetComponent(engine::ClassOf<engine::Light>())) : nullptr;
        engine::LightType type = light ? light->type() : engine::LightType::Point;
        f32 converted = LightUnitUtils::ConvertIntensity(type, unit_, value, intensity_, shapeRadius_);
        if (converted > 0 || intensity_ == 0)
        {
            intensity_ = converted;
            unit_ = value;
        }
    }

    f32 PhysicalAdditionalLightData::ShaderIntensity(engine::LightType type) const { return LightUnitUtils::ToShaderIntensity(type, unit_, intensity_, shapeRadius_); }

    PhysicalAdditionalCameraData::~PhysicalAdditionalCameraData() = default;

    PhysicalAdditionalLightData* FindPhysicalAdditionalLightData(const engine::Light* light)
    {
        if (!light || !light->gameObject())
            return nullptr;
        return static_cast<PhysicalAdditionalLightData*>(light->gameObject()->GetComponent(scripting::TypeOf<PhysicalAdditionalLightData>()));
    }

    PhysicalAdditionalCameraData* FindPhysicalAdditionalCameraData(const engine::Camera* camera)
    {
        if (!camera || !camera->gameObject())
            return nullptr;
        return static_cast<PhysicalAdditionalCameraData*>(camera->gameObject()->GetComponent(scripting::TypeOf<PhysicalAdditionalCameraData>()));
    }

    PhysicalAdditionalLightData* GetPhysicalAdditionalLightData(engine::Light* light)
    {
        if (PhysicalAdditionalLightData* d = FindPhysicalAdditionalLightData(light))
            return d;
        if (!light || !light->gameObject())
            return nullptr;
        auto* d = static_cast<PhysicalAdditionalLightData*>(light->gameObject()->AddComponent(scripting::TypeOf<PhysicalAdditionalLightData>()));
        if (d)
        {
            // The light's own intensity, read in its type's default unit.
            d->SetLightUnit(LightUnitUtils::DefaultUnit(light->type()));
            d->SetIntensity(light->intensity());
        }
        return d;
    }

    PhysicalAdditionalCameraData* GetPhysicalAdditionalCameraData(engine::Camera* camera)
    {
        if (PhysicalAdditionalCameraData* d = FindPhysicalAdditionalCameraData(camera))
            return d;
        if (!camera || !camera->gameObject())
            return nullptr;
        return static_cast<PhysicalAdditionalCameraData*>(camera->gameObject()->AddComponent(scripting::TypeOf<PhysicalAdditionalCameraData>()));
    }
}
