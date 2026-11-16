#include <Cpp/CppScripts.h>
#include <Support/PhysicalHost.h>
#include <Support/TestScene.h>

#include <Helix/Core/Logging/Log.h>
#include <Helix/Engine/Rendering/GraphicsBuffer.h>
#include <Helix/Engine/Shader/ComputeShader.h>
#include <Helix/RenderPipelines/Physical/Energy/EnergyIntegration.h>
#include <Helix/RenderPipelines/Physical/Energy/EnergyTables.inl>
#include <Helix/RenderPipelines/Physical/Lighting/LightUnits.h>
#include <Helix/Testing/Test.h>

#include <cmath>
#include <cstdio>

using namespace helix;
using namespace helix::engine;
using namespace helix::rendering;
using namespace helix::rendering::physical;
using namespace helix::rpphysical::testing;

namespace
{
    bool Near(f32 a, f32 b, f32 e = 1e-3f) { return std::fabs(a - b) <= e * Max(1.0f, std::fabs(b)); }

    struct ErrorCount
    {
        u32 errors = 0;
        ErrorCount() { AddLogSink(&Sink, this); }
        ~ErrorCount() { RemoveLogSink(&Sink, this); }
        static void Sink(const LogMessage& m, void* user)
        {
            if (m.level >= LogLevel::Error)
                static_cast<ErrorCount*>(user)->errors++;
        }
    };

    bool PipelineDevice(rhi::Device& device)
    {
        const rhi::DeviceCaps& c = device.Caps();
        return c.compute && c.storageBuffers && (c.backend == rhi::Backend::D3D11 || (c.backend == rhi::Backend::OpenGL && (c.apiMajor > 4 || c.apiMinor >= 5)));
    }
}

HX_TEST(Physical, LightUnitsAndExposureAgainstAnalyticValues)
{
    // Lumen to candela over the whole sphere, the inverse square law, EV100 and luminance (L = 2^EV * 12.5 / 100), the
    // exposure of f/1 at 1 s and ISO 100 (EV 0), and a sphere light's nits.
    HX_CHECK(Near(LightUnitUtils::LumenToCandela(4.0f * mathf::PI * 100.0f, LightUnitUtils::SolidAngle(LightType::Point)), 100.0f));
    HX_CHECK(Near(LightUnitUtils::CandelaToLux(400.0f, 2.0f), 100.0f) && Near(LightUnitUtils::LuxToCandela(100.0f, 2.0f), 400.0f));
    HX_CHECK(Near(LightUnitUtils::Ev100ToNits(3.0f), 1.0f) && Near(LightUnitUtils::NitsToEv100(1.0f), 3.0f));
    HX_CHECK(Near(LightUnitUtils::ComputeEV100(1.0f, 1.0f, 100.0f), 0.0f, 1e-5f) && Near(LightUnitUtils::ComputeEV100(16.0f, 1.0f / 125.0f, 100.0f), std::log2(256.0f * 125.0f)));
    HX_CHECK(Near(LightUnitUtils::EV100ToExposure(0.0f), 1.0f / 1.2f) && Near(LightUnitUtils::ExposureToEV100(LightUnitUtils::EV100ToExposure(7.5f)), 7.5f));
    HX_CHECK(Near(LightUnitUtils::NitsToCandela(1000.0f, 0.1f), 1000.0f * mathf::PI * 0.01f));
    // Units that do not fit a light type give nothing; the shaders' intensity of each.
    HX_CHECK(LightUnitUtils::ToShaderIntensity(LightType::Directional, LightUnit::Lumen, 100.0f, 0) == 0.0f);
    HX_CHECK(Near(LightUnitUtils::ToShaderIntensity(LightType::Spot, LightUnit::Ev100, 3.0f, 1.0f), mathf::PI));
    HX_CHECK(Near(LightUnitUtils::ConvertIntensity(LightType::Point, LightUnit::Candela, LightUnit::Lumen, 10.0f, 0), 40.0f * mathf::PI));
    // A color temperature keeps the intensity: its luminance is 1; warm is red, cold is blue.
    Color warm = LightUnitUtils::ColorTemperatureToLinear(2700), cold = LightUnitUtils::ColorTemperatureToLinear(12000);
    HX_CHECK(Near(0.2126729f * warm.r + 0.7151522f * warm.g + 0.072175f * warm.b, 1.0f) && warm.r > warm.b && cold.b > cold.r);

    // The same line from C# and from C++.
    HX_REQUIRE(StartScripts());
    GameObject* eye = GameObject::Create("UnitsEye");
    Camera* cam = eye->AddComponent<Camera>();
    cam->SetUsePhysicalProperties(true);
    cam->SetAperture(2.8f);
    cam->SetShutterSpeed(1.0f / 60.0f);
    cam->SetIso(400);
    GameObject* lamp = GameObject::Create("UnitsLamp");
    Light* light = lamp->AddComponent<Light>();
    light->SetType(LightType::Point);
    light->SetIntensity(50);
    PhysicalAdditionalLightData* data = GetPhysicalAdditionalLightData(light);
    LightUnit before = data->lightUnit();
    f32 candela = data->intensity();
    data->SetLightUnitAndConvert(LightUnit::Lumen);
    const char* names[] = {"Lumen", "Candela", "Lux", "Nits", "Ev100"};
    Notes().Clear();
    Notes().Push(Format("units:{}:{:.3f}:{}:{:.3f}:{:.3f}:{:.3f}:{:.3f}:{:.3f}", names[i32(before)], candela, names[i32(data->lightUnit())], data->intensity(),
                        LightUnitUtils::ComputeEV100(cam->aperture(), cam->shutterSpeed(), f32(cam->iso())), LightUnitUtils::Ev100ToNits(3),
                        LightUnitUtils::EV100ToExposure(0), LightUnitUtils::LumenToCandela(1000, LightUnitUtils::SolidAngle(LightType::Point))));
    DestroyImmediate(eye);
    DestroyImmediate(lamp);
    GameObject* host = GameObject::Create("UnitsHost");
    host->AddComponent(CSharp("CsPhysicalUnits").type);
    RunScriptPhase(ScriptMessage::Update);
    DestroyImmediate(host);
    const char* expected = "units:Candela:50.000:Lumen:628.319:6.878:1.000:0.833:79.577";
    HX_CHECK(SameNotes({expected, expected}));
}

HX_TEST(Physical, TheEnergyTablesAreTheToolsIntegrals)
{
    // Nodes across the tables integrated again: the embedded floats are the tool's doubles rounded.
    for (i32 j : {0, 7, 19, 31})
        for (i32 i : {0, 3, 16, 31})
        {
            f64 mu = energy::NodeCosine(i), r = energy::NodeRoughness(j);
            energy::GgxAlbedo g = energy::IntegrateGgx(mu, r);
            const f32* t = &energy::kTable[(j * energy::kSize + i) * 4];
            HX_CHECK(std::fabs(t[0] - g.a) < 1e-6 && std::fabs(t[1] - g.b) < 1e-6 && std::fabs(t[2] - g.c) < 1e-6);
            HX_CHECK(std::fabs(t[3] - energy::IntegrateFon(mu, r)) < 1e-6);
        }
    for (i32 j : {0, 15, 31})
        HX_CHECK(std::fabs(energy::kAverage[j * 2] - energy::AverageGgx(energy::NodeRoughness(j))) < 1e-6 &&
                 std::fabs(energy::kAverage[j * 2 + 1] - energy::AverageFon(energy::NodeRoughness(j))) < 1e-6);
    // What they must be: a mirror reflects all, Lambert (sigma 0) has albedo 1, rough lobes lose energy.
    HX_CHECK(std::fabs(energy::kTable[(1 * energy::kSize + 31) * 4] + energy::kTable[(1 * energy::kSize + 31) * 4 + 1] - 1.0f) < 0.01f);
    HX_CHECK(std::fabs(energy::kTable[(0 * energy::kSize + 10) * 4 + 3] - 1.0f) < 1e-4f);
    HX_CHECK(energy::kAverage[31 * 2] < 0.6f && energy::kAverage[31 * 2 + 1] < 0.95f && energy::kAverage[0] > 0.95f);
}

HX_TEST(Physical, TheSceneInBothLanguages)
{
    HX_REQUIRE(StartScripts());
    constexpr u32 size = 128;
    u32 drawn = 0;
    ForEachDevice([&](rhi::Device& device, StringView label) {
        ShaderFolder shaders;
        TestTarget target(device, size, size);
        if (!PipelineDevice(device))
        {
            // Devices without compute in every stage: an error, nothing drawn.
            SceneObjects& s = CppScene();
            BuildPhysicalScene(s);
            ErrorCount e;
            HX_REQUIRE(target.Render());
            HX_CHECK(e.errors == 1);
            s.Destroy();
            return;
        }
        drawn++;
        // The scene of the C++ script.
        GameObject* host = GameObject::Create("SceneHost");
        host->AddComponent(scripting::TypeOf<CppPhysicalScene>());
        RunScriptPhase(ScriptMessage::Update);
        // A first frame (its cascades fit the casters' bounds of the frame before), then the frame compared.
        HX_REQUIRE(target.Render());
        HX_REQUIRE(target.Render());
        const PhysicalFrameInfo& info = PhysicalRenderPipeline::LastFrame();
        HX_CHECK(info.punctualLights == 9 && info.directionalLights == 1 && info.clustersX == 4 && info.clustersY == 4 && info.clustersZ == 16 && info.shadows &&
                 !info.temporalAntialiasing && std::fabs(info.ev100 - 10.0f) < 1e-4f);
        if (!(info.punctualLights == 9 && info.shadows))
            std::printf("    lights %d/%d clusters %dx%dx%d shadows %d ev %f\n", info.punctualLights, info.directionalLights, info.clustersX, info.clustersY,
                        info.clustersZ, int(info.shadows), info.ev100);
        target.Debug(Format("scene-{}", device.Caps().backend == rhi::Backend::D3D11 ? "d3d" : "gl"));
        HX_CHECK(target.Matches("PhysicalScene", label));
        Array<u32> cpp = target.pixels;
        // The next frame of the same scene draws the same image.
        HX_REQUIRE(target.Render());
        u32 again = MaxDifference(cpp, target.pixels);
        if (again > 0)
            std::printf("    the next frame differs by %u\n", again);
        HX_CHECK(again == 0);
        DestroyImmediate(host);
        CppScene().Destroy();
        // The same scene made by the C# script: the same image.
        Notes().Clear();
        GameObject* csHost = GameObject::Create("CsSceneHost");
        csHost->AddComponent(CSharp("CsPhysicalScene").type);
        RunScriptPhase(ScriptMessage::Update);
        HX_CHECK(SameNotes({"scene:True"}));
        HX_REQUIRE(target.Render());
        HX_REQUIRE(target.Render());
        u32 diff = MaxDifference(cpp, target.pixels);
        if (diff > 0)
            std::printf("    the C# scene differs by %u\n", diff);
        HX_CHECK(diff == 0);
        DestroyImmediate(csHost);
        GraphicsSettings::SetDefaultRenderPipeline(nullptr);
        for (const char* name : {"Eye", "Sun", "Spot", "Ground", "Dielectric", "Metal", "Coated", "Anisotropic", "Emissive", "Volume"})
            while (GameObject* g = GameObject::Find(name))
                DestroyImmediate(g);
        for (i32 i = 0; i < 8; i++)
            while (GameObject* g = GameObject::Find(Format("Lamp{}", i)))
                DestroyImmediate(g);
    });
    std::printf("    %u pipeline devices\n", drawn);
}

namespace
{
    // A case of the furnace kernel (Tests/Shaders/Furnace.cs).
    struct FurnaceCase
    {
        Vector4 baseParams, baseColor, specularParams, specularColor, coatParams, coatColor;
    };

    struct FurnaceSurface
    {
        const char* name;
        // The albedo the integral must have: 1 (white, no energy lost), or the tables' (-1).
        f32 expected;
        f32 tolerance;
        FurnaceCase c;
    };

    FurnaceCase Surface(f32 baseWeight, Vector3 base, f32 diffuseRoughness, f32 metalness, f32 specularWeight, Vector3 specular, f32 roughness, f32 anisotropy,
                        f32 ior, f32 coatWeight, Vector3 coat, f32 coatRoughness, f32 coatIor, f32 darkening)
    {
        FurnaceCase c;
        c.baseParams = Vector4(baseWeight, diffuseRoughness, metalness, 0);
        c.baseColor = Vector4(base.x, base.y, base.z, specularWeight);
        c.specularParams = Vector4(roughness, anisotropy, ior, coatWeight);
        c.specularColor = Vector4(specular.x, specular.y, specular.z, coatRoughness);
        c.coatParams = Vector4(coatIor, darkening, 0, 0);
        c.coatColor = Vector4(coat.x, coat.y, coat.z, 0);
        return c;
    }
}

HX_TEST(Physical, WhiteFurnace)
{
    // Each lobe combination integrated over the hemisphere on the GPU (the BSDF times the cosine): white surfaces keep all
    // the energy (base dielectric with smooth and rough diffuse, metal, a coat over the base), colored ones lose what
    // their colors take; every integral matches the albedo the energy tables give (the image based lighting's).
    HX_REQUIRE(StartScripts());
    const Vector3 white(1, 1, 1);
    const FurnaceSurface surfaces[] = {
        {"dielectric", 1, 0.02f, Surface(1, white, 0, 0, 1, white, 0.5f, 0, 1.5f, 0, white, 0, 1.6f, 1)},
        {"rough diffuse", 1, 0.02f, Surface(1, white, 1, 0, 1, white, 0.3f, 0, 1.5f, 0, white, 0, 1.6f, 1)},
        {"metal", 1, 0.02f, Surface(1, white, 0, 1, 1, white, 0.3f, 0, 1.5f, 0, white, 0, 1.6f, 1)},
        {"rough metal", 1, 0.02f, Surface(1, white, 0, 1, 1, white, 0.9f, 0, 1.5f, 0, white, 0, 1.6f, 1)},
        {"coat", 1, 0.02f, Surface(1, white, 0.5f, 0, 1, white, 0.6f, 0, 1.5f, 1, white, 0.3f, 1.6f, 1)},
        {"anisotropic metal", 1, 0.05f, Surface(1, white, 0, 1, 1, white, 0.5f, 0.8f, 1.5f, 0, white, 0, 1.6f, 1)},
        {"grey", -1, 0.02f, Surface(1, Vector3(0.5f, 0.5f, 0.5f), 0.3f, 0, 1, white, 0.5f, 0, 1.5f, 0, white, 0, 1.6f, 1)},
        {"gold", -1, 0.02f, Surface(1, Vector3(1, 0.77f, 0.34f), 0, 1, 1, Vector3(1, 0.9f, 0.8f), 0.4f, 0, 1.5f, 0, white, 0, 1.6f, 1)},
        {"tinted coat", -1, 0.02f, Surface(1, Vector3(0.2f, 0.4f, 0.8f), 0, 0, 1, white, 0.5f, 0, 1.5f, 1, Vector3(0.6f, 0.6f, 0.6f), 0.3f, 1.6f, 1)},
    };
    const f32 views[] = {0.25f, 0.5f, 0.9f};
    u32 ran = 0;
    ForEachDevice([&](rhi::Device& device, StringView label) {
        if (!PipelineDevice(device))
            return;
        ShaderFolder shaders;
        TestTarget target(device, 32, 32);
        // A frame of the pipeline makes its energy tables (the globals the library reads).
        SceneObjects s;
        BuildPhysicalScene(s);
        HX_REQUIRE(target.Render());
        ComputeShader* cs = ComputeShader::Find("Tests/Physical/Furnace");
        HX_REQUIRE(cs != nullptr);
        Array<FurnaceCase> cases;
        for (const FurnaceSurface& f : surfaces)
            for (f32 mu : views)
            {
                FurnaceCase c = f.c;
                c.baseParams.w = mu;
                cases.Push(c);
            }
        GraphicsBuffer* in = GraphicsBuffer::Create(GraphicsBufferTarget::Structured, 0, i32(cases.Size()), i32(sizeof(FurnaceCase)));
        GraphicsBuffer* out = GraphicsBuffer::Create(GraphicsBufferTarget::Structured, 0, i32(cases.Size()) * 2, 16);
        HX_REQUIRE(in && out && in->SetData(cases.Data(), u32(cases.Size() * sizeof(FurnaceCase)), 0));
        i32 k = cs->FindKernel("Integrate");
        cs->SetVector("_FurnaceCount", Vector4(f32(cases.Size()), 0, 0, 0));
        cs->SetBuffer(k, "_FurnaceCases", in);
        cs->SetBuffer(k, "_FurnaceResults", out);
        cs->Dispatch(k, 1, 1, 1);
        Array<Vector4> results;
        results.Resize(cases.Size() * 2);
        HX_REQUIRE(out->GetData(results.Data(), u32(results.Size() * 16), 0));
        ran++;
        u32 i = 0;
        for (const FurnaceSurface& f : surfaces)
            for (f32 mu : views)
            {
                Vector4 integral = results[i * 2], table = results[i * 2 + 1];
                i++;
                bool ok = true;
                for (i32 c = 0; c < 3; c++)
                {
                    f32 want = f.expected > 0 ? f.expected : (&table.x)[c];
                    ok = ok && std::fabs((&integral.x)[c] - want) <= f.tolerance && std::fabs((&table.x)[c] - (&integral.x)[c]) <= f.tolerance;
                }
                if (!ok)
                    std::printf("    %.*s %s at %.2f: integral %.4f %.4f %.4f, tables %.4f %.4f %.4f\n", int(label.Size()), label.Data(), f.name, mu, integral.x, integral.y,
                                integral.z, table.x, table.y, table.z);
                HX_CHECK(ok);
            }
        GraphicsBuffer::Release(in);
        GraphicsBuffer::Release(out);
        cs->ClearVariants();
        s.Destroy();
    });
    std::printf("    %u pipeline devices\n", ran);
}
