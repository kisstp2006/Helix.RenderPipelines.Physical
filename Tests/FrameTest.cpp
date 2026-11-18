#include <Support/PhysicalHost.h>
#include <Support/GpuFrame.h>
#include <Support/TestScene.h>

#include <Helix/Engine/Rendering/RenderSettings.h>
#include <Helix/Engine/Time/Time.h>
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
    bool PipelineDevice(rhi::Device& device)
    {
        const rhi::DeviceCaps& c = device.Caps();
        return c.compute && c.storageBuffers && (c.backend == rhi::Backend::D3D11 || (c.backend == rhi::Backend::OpenGL && (c.apiMajor > 4 || c.apiMinor >= 5)));
    }

    u32 Channel(u32 pixel, u32 c) { return (pixel >> (c * 8)) & 255u; }

    // The display value (0 to 255) of a linear value: sRGB, rounded.
    i32 Display(f32 linear)
    {
        f32 c = linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
        return i32(std::lround(Clamp(c, 0.0f, 1.0f) * 255.0f));
    }

    // A white wall (base weight 0.5, no specular: a Lambert surface of albedo 0.5) facing the camera at 5 m, the camera
    // and a global volume; no environment light.
    struct Wall
    {
        SceneObjects s;
        Light* light = nullptr;
        Exposure* exposure = nullptr;
        Tonemapping* tonemapping = nullptr;

        Wall()
        {
            GameObject* eye = GameObject::Create("WallEye");
            s.made.Push(eye);
            s.camera = eye->AddComponent<Camera>();
            s.camera->SetFieldOfView(30);
            s.camera->SetClearFlags(CameraClearFlags::SolidColor);
            s.camera->SetBackgroundColor(Color(0, 0, 0, 1));
            Mesh* box = TestBox();
            s.assets.Push(box);
            Material* white = Material::Create(Shader::Find("Physical/OpenPBR Lite"));
            s.assets.Push(white);
            white->SetColor("_BaseColor", Color(1, 1, 1, 1));
            white->SetFloat("_BaseWeight", 0.5f);
            white->SetFloat("_SpecularWeight", 0.0f);
            GameObject* wall = GameObject::Create("Wall");
            s.made.Push(wall);
            wall->transform()->SetPosition(Vector3(0, 0, 5.05f));
            wall->transform()->SetLocalScale(Vector3(20, 20, 0.1f));
            wall->AddComponent<MeshFilter>()->SetSharedMesh(box);
            wall->AddComponent<MeshRenderer>()->SetSharedMaterial(white);
            GameObject* lamp = GameObject::Create("WallLight");
            s.made.Push(lamp);
            light = lamp->AddComponent<Light>();
            s.profile = static_cast<VolumeProfile*>(ScriptableObject::CreateInstance(scripting::TypeOf<VolumeProfile>()));
            s.assets.Push(s.profile);
            GameObject* holder = GameObject::Create("WallVolume");
            s.made.Push(holder);
            auto* volume = static_cast<Volume*>(holder->AddComponent(scripting::TypeOf<Volume>()));
            volume->SetIsGlobal(true);
            volume->SetSharedProfile(s.profile);
            exposure = s.profile->Add<Exposure>();
            tonemapping = s.profile->Add<Tonemapping>();
            tonemapping->mode.Override(TonemappingMode::None);
            s.asset = PhysicalRenderPipelineAsset::Create();
            s.assets.Push(s.asset);
            s.asset->SetEnvironmentLuminance(0);
            GraphicsSettings::SetDefaultRenderPipeline(s.asset);
        }

        ~Wall() { s.Destroy(); }
    };

    // A clock the tests move.
    struct StepClock : TimeClock
    {
        double now = 100;
        double Now() override { return now; }
    };
}

HX_TEST(Physical, UnitsInTheImage)
{
    // A Lambert wall of albedo 0.5 lit at normal incidence: radiance = 0.5 * E / pi; the frame is that times the
    // exposure 1 / (1.2 * 2^EV100), displayed in sRGB without tonemapping. The sun's lux, a point light's candela at 2 m
    // (inverse square), and the physical camera's EV100 from its aperture, shutter and ISO.
    HX_REQUIRE(StartScripts());
    u32 ran = 0;
    ForEachDevice([&](rhi::Device& device, StringView label) {
        if (!PipelineDevice(device))
            return;
        ran++;
        ShaderFolder shaders;
        TestTarget target(device, 16, 16);
        Wall wall;
        auto center = [&]() { return target.pixels[8 * 16 + 8]; };
        auto check = [&](const char* what, f32 radiance, f32 ev100) {
            HX_REQUIRE(target.Render());
            HX_REQUIRE(target.Render());
            i32 want = Display(radiance * LightUnitUtils::EV100ToExposure(ev100));
            u32 got = center();
            bool ok = std::abs(i32(Channel(got, 0)) - want) <= 2 && std::abs(i32(Channel(got, 1)) - want) <= 2 && std::abs(i32(Channel(got, 2)) - want) <= 2;
            if (!ok)
                std::printf("    %.*s %s: %08x, expected %d\n", int(label.Size()), label.Data(), what, got, want);
            HX_CHECK(ok);
        };
        // The sun: 1000 lux along the view.
        wall.light->SetType(LightType::Directional);
        GetPhysicalAdditionalLightData(wall.light)->SetLightUnit(LightUnit::Lux);
        GetPhysicalAdditionalLightData(wall.light)->SetIntensity(1000);
        wall.exposure->mode.Override(ExposureMode::Fixed);
        wall.exposure->fixedExposure.Override(9.5f);
        check("1000 lux", 0.5f * 1000.0f / mathf::PI, 9.5f);
        // The compensation brightens by its EV.
        wall.exposure->compensation.Override(1.0f);
        check("compensation", 0.5f * 1000.0f / mathf::PI, 8.5f);
        wall.exposure->compensation.Override(0.0f);
        // The physical camera: f/4, 1/60 s, ISO 100.
        wall.s.camera->SetAperture(4);
        wall.s.camera->SetShutterSpeed(1.0f / 60.0f);
        wall.s.camera->SetIso(100);
        wall.exposure->mode.Override(ExposureMode::UsePhysicalCamera);
        check("physical camera", 0.5f * 1000.0f / mathf::PI, std::log2(16.0f * 60.0f));
        // A point light of 1000 cd 2 m from the wall: 250 lux.
        wall.exposure->mode.Override(ExposureMode::Fixed);
        wall.light->SetType(LightType::Point);
        wall.light->SetRange(100);
        wall.light->transform()->SetPosition(Vector3(0, 0, 3));
        GetPhysicalAdditionalLightData(wall.light)->SetLightUnit(LightUnit::Candela);
        GetPhysicalAdditionalLightData(wall.light)->SetIntensity(1000);
        check("1000 cd at 2 m", 0.5f * 250.0f / mathf::PI, 9.5f);
        // The same light in lumen.
        GetPhysicalAdditionalLightData(wall.light)->SetLightUnitAndConvert(LightUnit::Lumen);
        check("4000 pi lm at 2 m", 0.5f * 250.0f / mathf::PI, 9.5f);
    });
    std::printf("    %u pipeline devices\n", ran);
}

HX_TEST(Physical, AutomaticExposure)
{
    // The histogram measures the wall (its luminance's EV100 within a bin); a cut takes the measure at once, the
    // progressive mode moves toward it by the speed and the frame's time, the fixed mode at once.
    HX_REQUIRE(StartScripts());
    StepClock clock;
    Time::SetClock(&clock);
    Time::BeginFrame();
    u32 ran = 0;
    ForEachDevice([&](rhi::Device& device, StringView label) {
        if (!PipelineDevice(device))
            return;
        ran++;
        ShaderFolder shaders;
        TestTarget target(device, 32, 32);
        Wall wall;
        wall.light->SetType(LightType::Directional);
        GetPhysicalAdditionalLightData(wall.light)->SetLightUnit(LightUnit::Lux);
        GetPhysicalAdditionalLightData(wall.light)->SetIntensity(1000);
        wall.exposure->mode.Override(ExposureMode::Automatic);
        wall.exposure->adaptationMode.Override(AdaptationMode::Progressive);
        wall.exposure->adaptationSpeedDarkToLight.Override(1.0f);
        wall.exposure->adaptationSpeedLightToDark.Override(1.0f);
        f32 measured = std::log2(0.5f * 1000.0f / mathf::PI * 8.0f);
        auto ev = [&]() {
            f32 exposure = 0, value = -100;
            PhysicalRenderPipeline::ReadExposure(wall.s.camera, exposure, value);
            return value;
        };
        // The first frame is a cut: the measure at once (the histogram's bins are 24 / 128 EV wide).
        HX_REQUIRE(target.Render());
        f32 first = ev();
        HX_CHECK(std::fabs(first - measured) < 0.2f);
        // Four times the light: two EV more, reached by 1 - e^(-1 * 0.25) in a quarter second.
        GetPhysicalAdditionalLightData(wall.light)->SetIntensity(4000);
        clock.now += 0.25;
        Time::BeginFrame();
        HX_REQUIRE(target.Render());
        f32 step = ev();
        f32 expected = first + (measured + 2.0f - first) * (1.0f - std::exp(-0.25f));
        HX_CHECK(std::fabs(step - expected) < 0.2f);
        // The fixed adaptation: at once.
        wall.exposure->adaptationMode.Override(AdaptationMode::Fixed);
        clock.now += 0.25;
        Time::BeginFrame();
        HX_REQUIRE(target.Render());
        f32 fixedEv = ev();
        HX_CHECK(std::fabs(fixedEv - (measured + 2.0f)) < 0.2f);
        // The limits clamp it (into the last bin of -6 to 10), the compensation takes EV off.
        wall.exposure->limitMax.Override(10.0f);
        wall.exposure->compensation.Override(1.0f);
        HX_REQUIRE(target.Render());
        f32 limited = ev();
        f32 lastBin = -6.0f + 127.5f / 128.0f * 16.0f;
        HX_CHECK(std::fabs(limited - (lastBin - 1.0f)) < 0.01f);
        if (std::fabs(first - measured) >= 0.2f || std::fabs(step - expected) >= 0.2f || std::fabs(fixedEv - measured - 2.0f) >= 0.2f ||
            std::fabs(limited - (lastBin - 1.0f)) >= 0.01f)
            std::printf("    %.*s: measured %.3f, first %.3f, step %.3f (expected %.3f), fixed %.3f, limited %.3f\n", int(label.Size()), label.Data(), measured,
                        first, step, expected, fixedEv, limited);
        // The frame is middle grey: L * exposure = 0.125 / 1.2 before the limit, displayed.
        wall.exposure->limitMax.Override(18.0f);
        wall.exposure->compensation.Override(0.0f);
        HX_REQUIRE(target.Render());
        HX_REQUIRE(target.Render());
        i32 grey = Display(0.125f / 1.2f);
        HX_CHECK(std::abs(i32(Channel(target.pixels[16 * 32 + 16], 1)) - grey) <= 4);
    });
    Time::SetClock(nullptr);
    Time::Reset();
    std::printf("    %u pipeline devices\n", ran);
}

HX_TEST(Physical, ClustersFindEveryLight)
{
    // A hundred point lights over a ground: fine clusters (16 pixel tiles, 32 slices) and one coarse cluster holding all
    // of them draw the same image (a cluster that missed a light would be darker there).
    HX_REQUIRE(StartScripts());
    u32 ran = 0;
    ForEachDevice([&](rhi::Device& device, StringView label) {
        if (!PipelineDevice(device))
            return;
        ran++;
        ShaderFolder shaders;
        TestTarget target(device, 96, 96);
        SceneObjects s;
        GameObject* eye = GameObject::Create("GridEye");
        s.made.Push(eye);
        s.camera = eye->AddComponent<Camera>();
        eye->transform()->SetPosition(Vector3(0, 6, -6));
        eye->transform()->SetRotation(Quaternion::Euler(45, 0, 0));
        s.camera->SetClearFlags(CameraClearFlags::SolidColor);
        s.camera->SetBackgroundColor(Color(0, 0, 0, 1));
        Mesh* box = TestBox();
        s.assets.Push(box);
        Material* ground = Material::Create(Shader::Find("Physical/OpenPBR Lite"));
        s.assets.Push(ground);
        ground->SetFloat("_SpecularRoughness", 0.6f);
        GameObject* floor = GameObject::Create("GridGround");
        s.made.Push(floor);
        floor->transform()->SetPosition(Vector3(0, -0.05f, 0));
        floor->transform()->SetLocalScale(Vector3(12, 0.1f, 12));
        floor->AddComponent<MeshFilter>()->SetSharedMesh(box);
        floor->AddComponent<MeshRenderer>()->SetSharedMaterial(ground);
        for (i32 i = 0; i < 100; i++)
        {
            GameObject* lamp = GameObject::Create(Format("GridLamp{}", i));
            s.made.Push(lamp);
            Light* l = lamp->AddComponent<Light>();
            l->SetType(i % 7 == 3 ? LightType::Spot : LightType::Point);
            l->SetSpotAngle(70);
            l->SetRange(1.6f);
            l->SetColor(Color::HSVToRGB(f32(i % 10) / 10.0f, 0.7f, 1));
            lamp->transform()->SetPosition(Vector3(f32(i % 10) - 4.5f, 0.3f + 0.05f * f32(i % 3), f32(i / 10) - 4.5f));
            lamp->transform()->SetRotation(Quaternion::Euler(90, 0, 0));
            GetPhysicalAdditionalLightData(l)->SetLightUnit(LightUnit::Lumen);
            GetPhysicalAdditionalLightData(l)->SetIntensity(400);
        }
        s.profile = static_cast<VolumeProfile*>(ScriptableObject::CreateInstance(scripting::TypeOf<VolumeProfile>()));
        s.assets.Push(s.profile);
        GameObject* holder = GameObject::Create("GridVolume");
        s.made.Push(holder);
        auto* volume = static_cast<Volume*>(holder->AddComponent(scripting::TypeOf<Volume>()));
        volume->SetIsGlobal(true);
        volume->SetSharedProfile(s.profile);
        Exposure* exposure = s.profile->Add<Exposure>();
        exposure->mode.Override(ExposureMode::Fixed);
        exposure->fixedExposure.Override(6.0f);
        s.profile->Add<Tonemapping>()->mode.Override(TonemappingMode::Neutral);
        s.asset = PhysicalRenderPipelineAsset::Create();
        s.assets.Push(s.asset);
        s.asset->SetEnvironmentLuminance(0);
        s.asset->SetMaxVisibleLights(128);
        s.asset->SetMaxLightsPerCluster(128);
        s.asset->SetClusterTileSize(16);
        s.asset->SetClusterDepthSlices(32);
        GraphicsSettings::SetDefaultRenderPipeline(s.asset);
        HX_REQUIRE(target.Render());
        HX_REQUIRE(target.Render());
        const PhysicalFrameInfo& fine = PhysicalRenderPipeline::LastFrame();
        HX_CHECK(fine.punctualLights == 100 && fine.clustersX == 6 && fine.clustersZ == 32);
        Array<u32> clustered = target.pixels;
        HX_CHECK(target.Matches("PhysicalClusters", label));
        s.asset->SetClusterTileSize(256);
        s.asset->SetClusterDepthSlices(4);
        ResetRenderPipeline(s.asset);
        HX_REQUIRE(target.Render());
        HX_REQUIRE(target.Render());
        u32 diff = MaxDifference(clustered, target.pixels);
        if (diff > 2)
            std::printf("    %.*s: the coarse clusters differ by %u\n", int(label.Size()), label.Data(), diff);
        HX_CHECK(diff <= 2);
        s.Destroy();
    });
    std::printf("    %u pipeline devices\n", ran);
}

HX_TEST(Physical, TemporalAntialiasing)
{
    // The test scene with TAA: the jittered frames converge (two frames a whole sequence apart are the same), the edges
    // are smoothed while flat areas keep their color, a turned camera's history is reprojected (the frame stays close
    // to the frame without TAA), and a reset starts the history anew.
    HX_REQUIRE(StartScripts());
    constexpr u32 size = 128;
    u32 ran = 0;
    ForEachDevice([&](rhi::Device& device, StringView label) {
        if (!PipelineDevice(device))
            return;
        ran++;
        ShaderFolder shaders;
        TestTarget target(device, size, size);
        SceneObjects s;
        BuildPhysicalScene(s);
        HX_REQUIRE(target.Render());
        HX_REQUIRE(target.Render());
        Array<u32> plain = target.pixels;
        PhysicalAdditionalCameraData* extra = GetPhysicalAdditionalCameraData(s.camera);
        extra->SetAntialiasing(AntialiasingMode::TemporalAntialiasing);
        for (i32 i = 0; i < 32; i++)
            HX_REQUIRE(target.Render());
        HX_CHECK(PhysicalRenderPipeline::LastFrame().temporalAntialiasing);
        Array<u32> converged = target.pixels;
        // The average of the jittered frames is sensitive where samples fall on an edge (the box's top lies on a pixel
        // row) and on the sharp highlights: WARP rasterizes those samples a little differently and its average differs
        // by up to 32 there; the checks below keep their own tight limits.
        HX_CHECK(target.Matches("PhysicalSceneTaa", label, 40));
        for (u32 i = 0; i < TemporalAA::kSampleCount; i++)
            HX_REQUIRE(target.Render());
        u32 stable = MaxDifference(converged, target.pixels);
        if (stable > 3)
            std::printf("    %.*s: the converged frames differ by %u\n", int(label.Size()), label.Data(), stable);
        HX_CHECK(stable <= 3);
        // Flat areas keep their color: the ground's middle.
        u32 flat = 0;
        for (u32 y = 82; y < 95; y++)
            for (u32 x = 40; x < 90; x++)
                for (u32 c = 0; c < 3; c++)
                    flat = Max(flat, u32(std::abs(i32(Channel(converged[y * size + x], c)) - i32(Channel(plain[y * size + x], c)))));
        if (flat > 6)
            std::printf("    %.*s: the flat ground differs by %u\n", int(label.Size()), label.Data(), flat);
        HX_CHECK(flat <= 6);
        // A turned camera: its history reprojected stays close to the frame without TAA.
        s.camera->transform()->SetRotation(Quaternion::Euler(12, 3, 0));
        HX_REQUIRE(target.Render());
        Array<u32> turned = target.pixels;
        extra->SetAntialiasing(AntialiasingMode::None);
        HX_REQUIRE(target.Render());
        u64 sum = 0;
        for (usize i = 0; i < turned.Size(); i++)
            for (u32 c = 0; c < 3; c++)
                sum += u64(std::abs(i32(Channel(turned[i], c)) - i32(Channel(target.pixels[i], c))));
        f32 mean = f32(sum) / f32(turned.Size() * 3);
        if (mean > 3.0f)
            std::printf("    %.*s: the turned camera's frame differs by %.2f on average\n", int(label.Size()), label.Data(), mean);
        HX_CHECK(mean <= 3.0f);
        s.Destroy();
    });
    std::printf("    %u pipeline devices\n", ran);
}

HX_TEST(Physical, BindlessAndBoundTexturesDrawTheSameImage)
{
    // The test scene with textured materials: through the device's bindless table and through bindings, the same image.
    HX_REQUIRE(StartScripts());
    u32 ran = 0;
    ForEachDevice([&](rhi::Device& device, StringView label) {
        if (!PipelineDevice(device) || !device.Caps().bindlessTextures || device.Caps().backend != rhi::Backend::OpenGL)
            return;
        ran++;
        ShaderFolder shaders;
        TestTarget target(device, 128, 128);
        SceneObjects s;
        BuildPhysicalScene(s);
        Texture2D* checker = Texture2D::Create(32, 32, TextureFormat::RGBA32, false, false);
        s.assets.Push(checker);
        Texture2D* rough = Texture2D::Create(32, 32, TextureFormat::RGBA32, false, true);
        s.assets.Push(rough);
        Array<Color> pixels, roughness;
        for (i32 y = 0; y < 32; y++)
            for (i32 x = 0; x < 32; x++)
            {
                bool odd = ((x / 4) + (y / 4)) % 2 != 0;
                pixels.Push(odd ? Color(0.9f, 0.9f, 0.9f, 1) : Color(0.2f, 0.4f, 0.3f, 1));
                roughness.Push(Color(0, odd ? 0.3f : 1.0f, 1, 1));
            }
        checker->SetPixels(Span<const Color>(pixels.Data(), pixels.Size()));
        checker->Apply(false);
        rough->SetPixels(Span<const Color>(roughness.Data(), roughness.Size()));
        rough->Apply(false);
        for (Object* o : s.assets)
            if (Material* m = o->Is<Material>() ? static_cast<Material*>(o) : nullptr)
            {
                m->SetTexture("_BaseColorMap", checker);
                m->SetTexture("_RoughnessMetalnessMap", rough);
            }
        HX_REQUIRE(target.Render());
        HX_REQUIRE(target.Render());
        HX_CHECK(PhysicalRenderPipeline::LastFrame().bindless);
        Array<u32> bindless = target.pixels;
        HX_CHECK(target.Matches("PhysicalSceneTextured", label));
        s.asset->SetBindlessMaterials(false);
        HX_REQUIRE(target.Render());
        HX_REQUIRE(target.Render());
        HX_CHECK(!PhysicalRenderPipeline::LastFrame().bindless);
        u32 diff = MaxDifference(bindless, target.pixels);
        if (diff > 0)
            std::printf("    %.*s: bindless and bound differ by %u\n", int(label.Size()), label.Data(), diff);
        HX_CHECK(diff == 0);
        s.Destroy();
    });
    std::printf("    %u devices with bindless textures\n", ran);
}

HX_TEST(Physical, GpuBudgetAt1080p)
{
    // The test scene at 1920 x 1080 with TAA: the GPU time of its frames and their passes on every device; with
    // HELIX_GPU_BUDGET=1 (the hardware run of the exit check) a frame takes at most 16.6 ms (60 fps).
    HX_REQUIRE(StartScripts());
    bool checked = engine::testing::GpuBudgetChecked();
    u32 ran = 0;
    ForEachDevice([&](rhi::Device& device, StringView label) {
        if (!PipelineDevice(device) || !device.Caps().gpuTimers)
            return;
        ran++;
        ShaderFolder shaders;
        TestTarget target(device, 1920, 1080);
        SceneObjects s;
        BuildPhysicalScene(s);
        GetPhysicalAdditionalCameraData(s.camera)->SetAntialiasing(AntialiasingMode::TemporalAntialiasing);
        auto draw = [] {
            UpdateWorldMatrices();
            Graphics::RenderFrame();
        };
        // The first frames make the shaders' variants and the pools.
        engine::testing::TimeGpuFrames(device, 4, draw);
        engine::testing::GpuFrameTimes times = engine::testing::TimeGpuFrames(device, 16, draw);
        HX_REQUIRE(!times.frames.Empty());
        engine::testing::PrintGpuFrames(label, times);
        HX_CHECK(!checked || times.Median() <= 16.6);
        s.Destroy();
    });
    std::printf("    %u devices with GPU timers%s\n", ran, checked ? ", budget checked" : "");
}
