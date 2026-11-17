# Helix.RenderPipelines.Physical

The Physical render pipeline of the Helix engine: a physically based pipeline for desktop GPUs (Direct3D 11 feature
level 11 and OpenGL 4.5), with OpenPBR Surface materials, light in physical units, a physical camera, clustered
forward lighting computed on the GPU, automatic exposure and temporal antialiasing. It is written in C++ over the
engine and the shared render pipeline core (Helix.RenderPipelines.Core), with C# bindings (namespace
HelixEngine.Rendering.Physical). Its shaders are C#, compiled from their IL by the engine when they load, and call the
core's shader library.

The pipeline is a native module (`HelixRenderPipelinesPhysical`, a shared library beside the engine's and the core's;
a static one on the Web). Its C# side (`Helix.RenderPipelines.Physical.Runtime`) holds the generated bindings and the
C# written by hand over them. On devices without compute shaders and storage buffers in every stage it draws nothing
and logs why.

## What it draws

- Light in physical units: directional lights in lux, point and spot lights in lumen, candela, nits or EV100
  (`PhysicalAdditionalLightData`), color temperatures in Kelvin, inverse square falloff with a smooth window at the
  light's range, the sun's angular diameter widening its highlights.
- The physical camera: the engine camera's aperture, shutter speed and ISO give the exposure (EV100); the frame is
  pre-exposed, so radiance in nits stays in the range of a half float target.
- Clustered forward lighting: a depth and normal prepass, the view cut into screen tiles and logarithmic depth slices,
  a compute pass listing the point and spot lights of each cluster, the forward pass reading its pixel's list.
- The sun's cascaded shadows (1 to 4 cascades, soft shadows), shared with the Standard pipeline through the core.
- OpenPBR Surface's base, specular and coat layers (`Physical/OpenPBR Lite`): the EON rough diffuse model, metals with
  the F82 tint Fresnel, the GGX lobe with height correlated masking, anisotropy and multiple scattering compensation,
  the coat's absorption, darkening and roughening of the base, emission in nits; `Physical/Unlit`.
- The energy tables behind the albedo scaling and the compensation, made by the repo's own tool
  (`Tools/EnergyTables`) and embedded.
- The environment: the skybox's cube or the background color, the ambient probe and the reflections at a luminance the
  asset sets.
- Automatic exposure: a luminance histogram of the frame in compute, its average between two percentages, clamped,
  compensated and reached over time; or a fixed EV100, or the physical camera's (the `Exposure` volume component).
- Temporal antialiasing: a Halton jitter of the projection, the history reprojected by the camera's motion and clamped
  to the frame's neighbourhood, started anew on a cut.
- Tonemapping (Neutral, ACES) through the `Tonemapping` volume component.
- Bindless materials: the OpenPBR maps read through the device's bindless textures where it has them (OpenGL with
  ARB_bindless_texture), bound textures elsewhere, with the same image.
- Display colors drawn into the scene (world space UI panels) made linear by the global `_HelixLinearOutput` the
  pipeline sets: a panel's white is the frame's pre-exposed 1, tone mapped with the scene.

## Contents

| Path | Content |
|---|---|
| `Source/Helix/RenderPipelines/Physical/Settings/` | The pipeline's enums: light units, exposure and adaptation modes, tonemapping, antialiasing, shadow sizes |
| `Source/Helix/RenderPipelines/Physical/Pipeline/` | `PhysicalRenderPipelineAsset` (the clustering, shadow, environment and material settings) and `PhysicalRenderPipeline` (each camera's frame on the core's render graph) |
| `Source/Helix/RenderPipelines/Physical/Lighting/` | `LightUnitUtils` (photometric units and exposure values) and the light loop (the lights of a frame, their clusters) |
| `Source/Helix/RenderPipelines/Physical/Exposure/` | The fixed, physical and automatic exposure |
| `Source/Helix/RenderPipelines/Physical/Temporal/` | TAA's jitter, history and resolve pass |
| `Source/Helix/RenderPipelines/Physical/Energy/` | The integrals of the energy tables and the tables the tool wrote |
| `Source/Helix/RenderPipelines/Physical/Components/` | `PhysicalAdditionalLightData`, `PhysicalAdditionalCameraData` |
| `Source/Helix/RenderPipelines/Physical/Overrides/` | The volume components `Exposure` and `Tonemapping` |
| `Source/Helix.RenderPipelines.Physical.Runtime/` | The C# side: the generated bindings and the C# written over them |
| `Shaders/` | The C# shaders and their library (`Shaders/Library/`): OpenPBR Lite, Unlit, the light clustering and exposure kernels, the sky, TAA and the final pass |
| `Tools/EnergyTables/` | The tool that writes the energy tables |
| `Tests/` | Tests in both languages run in the engine by a C++ test program on every device the pipeline supports (each D3D11 adapter, the newest OpenGL) and one it refuses (OpenGL 3.3): white furnace tests of the BSDF on the GPU, the energy tables against the tool, physical units against analytic values, the automatic exposure, the clusters, TAA, bindless against bound textures, the GPU time of a 1920 x 1080 frame and its passes (held to 16.6 ms with `HELIX_GPU_BUDGET=1`), golden images in `Tests/Golden/`; `Tests/Api/Helix.RenderPipelines.Physical.csv` is the inventory of the public C# surface |

## Use

In C++: an asset of the pipeline, a light in physical units and the exposure of a global volume.

```cpp
#include <Helix/RenderPipelines/Core/Volume/Volume.h>
#include <Helix/RenderPipelines/Physical/Components/AdditionalData.h>
#include <Helix/RenderPipelines/Physical/Overrides/Overrides.h>
#include <Helix/RenderPipelines/Physical/Pipeline/PhysicalRenderPipelineAsset.h>

using namespace helix;
using namespace helix::rendering;
using namespace helix::rendering::physical;

PhysicalRenderPipelineAsset* asset = PhysicalRenderPipelineAsset::Create();
asset->SetShadowCascadeCount(4);
engine::GraphicsSettings::SetDefaultRenderPipeline(asset);

PhysicalAdditionalLightData* sun = GetPhysicalAdditionalLightData(sunLight);
sun->SetLightUnit(LightUnit::Lux);
sun->SetIntensity(100000);

Exposure* exposure = profile->Add<Exposure>();
exposure->mode.Override(ExposureMode::Automatic);
```

In C#, the same:

```csharp
using HelixEngine;
using HelixEngine.Rendering;
using HelixEngine.Rendering.Physical;

var asset = PhysicalRenderPipelineAsset.Create();
GraphicsSettings.defaultRenderPipeline = asset;

PhysicalAdditionalLightData sun = sunLight.GetPhysicalAdditionalLightData();
sun.lightUnit = LightUnit.Lux;
sun.intensity = 100000;

camera.GetPhysicalAdditionalCameraData().antialiasing = AntialiasingMode.TemporalAntialiasing;
```

## Building

The repository builds with the presets of Helix.CMake (`cmake --preset windows-debug`, `windows-release`,
`web-release`, `android-arm64-debug`); its dependencies are submodules in `External/`. The energy tables are written
again with the `helix_physical_energy_tables_update` target.

## License

GPL-3.0, see `LICENSE`.
