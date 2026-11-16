#pragma once

#include <Support/Golden.h>
#include <Support/PhysicalHost.h>

#include <Helix/Engine/GameObject/GameObject.h>
#include <Helix/Engine/GameObject/Lifecycle.h>
#include <Helix/Engine/Material/Material.h>
#include <Helix/Engine/Mesh/Mesh.h>
#include <Helix/Engine/Rendering/Camera.h>
#include <Helix/Engine/Rendering/Graphics.h>
#include <Helix/Engine/Rendering/Light.h>
#include <Helix/Engine/Rendering/Renderer.h>
#include <Helix/Engine/Script/ScriptableObject.h>
#include <Helix/Engine/Transform/Hierarchy.h>
#include <Helix/Engine/Transform/Transform.h>
#include <Helix/RenderPipelines/Core/Volume/Volume.h>
#include <Helix/RenderPipelines/Physical/Components/AdditionalData.h>
#include <Helix/RenderPipelines/Physical/Overrides/Overrides.h>
#include <Helix/RenderPipelines/Physical/Pipeline/PhysicalRenderPipeline.h>

#include <cmath>

// The scene of the pipeline's frame tests (the C# script CsPhysicalScene makes the same one) and the device's target
// they draw into.
namespace helix::rpphysical::testing
{
    // A box of side 1 around the origin (faces clockwise seen from outside, with normals, tangents and uvs).
    inline engine::Mesh* TestBox()
    {
        Array<Vector3> v, n;
        Array<Vector2> uv;
        Array<i32> t;
        const Vector3 axes[3] = {Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1)};
        for (i32 a = 0; a < 3; a++)
            for (f32 sign : {1.0f, -1.0f})
            {
                Vector3 normal = axes[a] * sign;
                Vector3 u = axes[(a + 1) % 3] * 0.5f, w = axes[(a + 2) % 3] * 0.5f;
                if (sign < 0)
                    std::swap(u, w);
                i32 base = i32(v.Size());
                Vector3 c = normal * 0.5f;
                for (Vector3 p : {c - u - w, c - u + w, c + u + w, c + u - w})
                    v.Push(p), n.Push(normal);
                for (Vector2 q : {Vector2(0, 0), Vector2(0, 1), Vector2(1, 1), Vector2(1, 0)})
                    uv.Push(q);
                for (i32 k : {0, 2, 1, 0, 3, 2})
                    t.Push(base + k);
            }
        engine::Mesh* m = engine::Mesh::Create();
        m->SetVertices(Span<const Vector3>(v.Data(), v.Size()));
        m->SetNormals(Span<const Vector3>(n.Data(), n.Size()));
        m->SetUVs(0, Span<const Vector2>(uv.Data(), uv.Size()));
        m->SetTriangles(Span<const i32>(t.Data(), t.Size()));
        m->RecalculateTangents();
        m->RecalculateBounds();
        return m;
    }

    // A sphere of radius 0.5 around the origin (clockwise from outside), with normals, tangents and uvs.
    inline engine::Mesh* TestSphere(i32 rings = 16, i32 segments = 32)
    {
        Array<Vector3> v, n;
        Array<Vector2> uv;
        Array<i32> t;
        for (i32 r = 0; r <= rings; r++)
            for (i32 s = 0; s <= segments; s++)
            {
                f32 theta = mathf::PI * f32(r) / f32(rings), phi = 2.0f * mathf::PI * f32(s) / f32(segments);
                Vector3 d(std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi));
                v.Push(d * 0.5f), n.Push(d), uv.Push(Vector2(f32(s) / f32(segments), 1.0f - f32(r) / f32(rings)));
            }
        for (i32 r = 0; r < rings; r++)
            for (i32 s = 0; s < segments; s++)
            {
                i32 a = r * (segments + 1) + s, b = a + segments + 1;
                for (i32 k : {a, a + 1, b, a + 1, b + 1, b})
                    t.Push(k);
            }
        engine::Mesh* m = engine::Mesh::Create();
        m->SetVertices(Span<const Vector3>(v.Data(), v.Size()));
        m->SetNormals(Span<const Vector3>(n.Data(), n.Size()));
        m->SetUVs(0, Span<const Vector2>(uv.Data(), uv.Size()));
        m->SetTriangles(Span<const i32>(t.Data(), t.Size()));
        m->RecalculateTangents();
        m->RecalculateBounds();
        return m;
    }

    // What a scene made: its objects and assets (destroyed by the caller).
    struct SceneObjects
    {
        Array<engine::GameObject*> made;
        Array<engine::Object*> assets;
        engine::Camera* camera = nullptr;
        rendering::physical::PhysicalRenderPipelineAsset* asset = nullptr;
        rendering::VolumeProfile* profile = nullptr;

        void Destroy()
        {
            engine::GraphicsSettings::SetDefaultRenderPipeline(nullptr);
            engine::Graphics::ReleaseResources();
            for (engine::GameObject* g : made)
                engine::DestroyImmediate(g);
            for (engine::Object* o : assets)
                engine::DestroyImmediate(o);
            made.Clear();
            assets.Clear();
        }
    };

    // The test scene: a camera, the sun (5000 lux, soft shadows), eight colored point lights on a ring (3000 lm) and a
    // warm spot light (3000 K, 60000 lm), a ground, four spheres (rough red dielectric, gold, a coated blue, brushed
    // silver), an emissive box (5000 nits), a global volume (fixed EV100 10, ACES), the pipeline's asset set.
    inline void BuildPhysicalScene(SceneObjects& s)
    {
        using namespace engine;
        using namespace rendering;
        using namespace rendering::physical;
        GameObject* eye = GameObject::Create("Eye");
        s.made.Push(eye);
        s.camera = eye->AddComponent<Camera>();
        eye->transform()->SetPosition(Vector3(0, 1.6f, -6.5f));
        eye->transform()->SetRotation(Quaternion::Euler(12, 0, 0));
        s.camera->SetFieldOfView(45);
        s.camera->SetNearClipPlane(0.1f);
        s.camera->SetFarClipPlane(100);
        s.camera->SetClearFlags(CameraClearFlags::SolidColor);
        s.camera->SetBackgroundColor(Color(0.02f, 0.03f, 0.06f, 1));

        GameObject* sun = GameObject::Create("Sun");
        s.made.Push(sun);
        Light* sunLight = sun->AddComponent<Light>();
        sunLight->SetType(LightType::Directional);
        sunLight->SetShadows(LightShadows::Soft);
        sun->transform()->SetRotation(Quaternion::Euler(45, 30, 0));
        PhysicalAdditionalLightData* sunData = GetPhysicalAdditionalLightData(sunLight);
        sunData->SetLightUnit(LightUnit::Lux);
        sunData->SetIntensity(5000);

        for (i32 i = 0; i < 8; i++)
        {
            GameObject* lamp = GameObject::Create(Format("Lamp{}", i));
            s.made.Push(lamp);
            Light* l = lamp->AddComponent<Light>();
            l->SetType(LightType::Point);
            l->SetRange(4);
            l->SetColor(Color::HSVToRGB(f32(i) / 8.0f, 0.8f, 1));
            f32 angle = 2.0f * mathf::PI * f32(i) / 8.0f;
            lamp->transform()->SetPosition(Vector3(std::cos(angle) * 2.5f, 0.4f, 0.5f + std::sin(angle) * 2.5f));
            PhysicalAdditionalLightData* d = GetPhysicalAdditionalLightData(l);
            d->SetLightUnit(LightUnit::Lumen);
            d->SetIntensity(3000);
            d->SetShapeRadius(0.05f);
        }
        GameObject* spot = GameObject::Create("Spot");
        s.made.Push(spot);
        Light* spotLight = spot->AddComponent<Light>();
        spotLight->SetType(LightType::Spot);
        spotLight->SetRange(8);
        spotLight->SetSpotAngle(50);
        spotLight->SetInnerSpotAngle(30);
        spotLight->SetUseColorTemperature(true);
        spotLight->SetColorTemperature(3000);
        spot->transform()->SetPosition(Vector3(0, 3, -1));
        spot->transform()->SetRotation(Quaternion::Euler(60, 0, 0));
        PhysicalAdditionalLightData* spotData = GetPhysicalAdditionalLightData(spotLight);
        spotData->SetLightUnit(LightUnit::Lumen);
        spotData->SetIntensity(60000);

        Mesh* box = TestBox();
        Mesh* sphere = TestSphere();
        s.assets.Push(box), s.assets.Push(sphere);
        auto material = [&](Color base) {
            Material* m = Material::Create(Shader::Find("Physical/OpenPBR Lite"));
            m->SetColor("_BaseColor", base);
            s.assets.Push(m);
            return m;
        };
        auto thing = [&](StringView name, Mesh* mesh, Material* m, Vector3 position, Vector3 scale) {
            GameObject* g = GameObject::Create(name);
            g->transform()->SetPosition(position);
            g->transform()->SetLocalScale(scale);
            g->AddComponent<MeshFilter>()->SetSharedMesh(mesh);
            g->AddComponent<MeshRenderer>()->SetSharedMaterial(m);
            s.made.Push(g);
            return g;
        };
        Material* ground = material(Color(0.5f, 0.5f, 0.5f, 1));
        ground->SetFloat("_SpecularRoughness", 0.8f);
        thing("Ground", box, ground, Vector3(0, -0.05f, 1), Vector3(8, 0.1f, 8));
        Material* red = material(Color(0.8f, 0.1f, 0.1f, 1));
        red->SetFloat("_SpecularRoughness", 0.4f);
        red->SetFloat("_BaseDiffuseRoughness", 0.5f);
        thing("Dielectric", sphere, red, Vector3(-2.1f, 0.6f, 0.5f), Vector3(1.2f, 1.2f, 1.2f));
        Material* gold = material(Color(1, 0.77f, 0.34f, 1));
        gold->SetFloat("_BaseMetalness", 1);
        gold->SetColor("_SpecularColor", Color(1, 0.9f, 0.8f, 1));
        gold->SetFloat("_SpecularRoughness", 0.25f);
        thing("Metal", sphere, gold, Vector3(-0.7f, 0.6f, 0.5f), Vector3(1.2f, 1.2f, 1.2f));
        Material* coated = material(Color(0.05f, 0.1f, 0.6f, 1));
        coated->SetFloat("_SpecularRoughness", 0.6f);
        coated->SetFloat("_CoatWeight", 1);
        coated->SetFloat("_CoatRoughness", 0.05f);
        thing("Coated", sphere, coated, Vector3(0.7f, 0.6f, 0.5f), Vector3(1.2f, 1.2f, 1.2f));
        Material* silver = material(Color(0.95f, 0.95f, 0.95f, 1));
        silver->SetFloat("_BaseMetalness", 1);
        silver->SetFloat("_SpecularRoughness", 0.4f);
        silver->SetFloat("_SpecularRoughnessAnisotropy", 0.8f);
        thing("Anisotropic", sphere, silver, Vector3(2.1f, 0.6f, 0.5f), Vector3(1.2f, 1.2f, 1.2f));
        Material* glow = material(Color(0.1f, 0.1f, 0.1f, 1));
        glow->EnableKeyword("_EMISSION");
        glow->SetColor("_EmissionColor", Color(1, 0.5f, 0.1f, 1));
        glow->SetFloat("_EmissionLuminance", 5000);
        thing("Emissive", box, glow, Vector3(0, 0.3f, -1.5f), Vector3(0.4f, 0.4f, 0.4f));

        // The exposure and tonemapping of a global volume.
        s.profile = static_cast<VolumeProfile*>(ScriptableObject::CreateInstance(scripting::TypeOf<VolumeProfile>()));
        s.assets.Push(s.profile);
        GameObject* holder = GameObject::Create("Volume");
        s.made.Push(holder);
        auto* volume = static_cast<Volume*>(holder->AddComponent(scripting::TypeOf<Volume>()));
        volume->SetIsGlobal(true);
        volume->SetSharedProfile(s.profile);
        Exposure* exposure = s.profile->Add<Exposure>();
        exposure->mode.Override(ExposureMode::Fixed);
        exposure->fixedExposure.Override(10.0f);
        Tonemapping* tonemapping = s.profile->Add<Tonemapping>();
        tonemapping->mode.Override(TonemappingMode::ACES);

        s.asset = PhysicalRenderPipelineAsset::Create();
        s.assets.Push(s.asset);
        s.asset->SetShadowDistance(20);
        s.asset->SetShadowmapResolution(1024);
        s.asset->SetEnvironmentLuminance(1000);
        s.asset->SetMaxVisibleLights(64);
        s.asset->SetClusterTileSize(32);
        GraphicsSettings::SetDefaultRenderPipeline(s.asset);
    }

    // The device's color and depth target of a frame test.
    struct TestTarget
    {
        rhi::Device& device;
        rhi::TextureHandle color, depth;
        u32 width, height;
        Array<u32> pixels;

        TestTarget(rhi::Device& d, u32 w, u32 h) : device(d), width(w), height(h)
        {
            color = device.CreateTexture({.format = rhi::Format::RGBA8Unorm, .width = w, .height = h, .usage = rhi::TextureRenderTarget | rhi::TextureSampled});
            depth = device.CreateTexture({.format = rhi::Format::D32Float, .width = w, .height = h, .usage = rhi::TextureDepthStencil});
            engine::Graphics::SetDevice(&device);
            engine::Graphics::SetTarget({color, depth, w, h, rhi::Format::RGBA8Unorm, rhi::Format::D32Float});
            engine::Camera::SetDisplaySize(w, h);
            pixels.Resize(w * h);
        }

        // A frame rendered and read back.
        bool Render()
        {
            engine::UpdateWorldMatrices();
            engine::Graphics::RenderFrame();
            return device.ReadTexture(color, 0, pixels.Data(), pixels.Size() * 4);
        }

        // HELIX_DEBUG_IMAGES=<folder>: the image written there as <name>.png.
        void Debug(StringView name) const
        {
            const char* folder = std::getenv("HELIX_DEBUG_IMAGES");
            if (!folder)
                return;
            image::Image img;
            img.Allocate(width, height, image::ImageFormat::RGBA8);
            std::memcpy(img.pixels.Data(), pixels.Data(), img.pixels.Size());
            Array<u8> file;
            image::EncodePng(img, file, 6);
            platform::WriteFile(platform::JoinPath(folder, Format("{}.png", name)), file);
        }

        bool Matches(const char* golden, StringView label, u32 tolerance = 12)
        {
            bool same = engine::testing::MatchGolden(golden, pixels.Data(), width, height, tolerance);
            if (!same)
                std::printf("    %s differs on %.*s\n", golden, int(label.Size()), label.Data());
            return same;
        }

        ~TestTarget()
        {
            engine::GraphicsSettings::SetDefaultRenderPipeline(nullptr);
            engine::Camera::SetDisplaySize(1, 1);
            engine::Graphics::ReleaseResources();
            device.Destroy(color);
            device.Destroy(depth);
        }
    };

    // The largest difference of a channel between two images.
    inline u32 MaxDifference(const Array<u32>& a, const Array<u32>& b)
    {
        u32 most = 0;
        for (usize i = 0; i < a.Size() && i < b.Size(); i++)
            for (u32 shift = 0; shift < 32; shift += 8)
            {
                i32 d = i32((a[i] >> shift) & 255u) - i32((b[i] >> shift) & 255u);
                most = Max(most, u32(d < 0 ? -d : d));
            }
        return a.Size() == b.Size() ? most : 255u;
    }
}
