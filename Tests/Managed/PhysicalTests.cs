using System.Collections.Generic;
using System.Globalization;
using System.Text;
using Helix.Scripting;
using HelixEngine;
using HelixEngine.Rendering;
using HelixEngine.Rendering.Physical;

namespace Tests
{
    // The notes the C# tests write into the C++ test program's list (its export by name).
    static unsafe class Notes
    {
        static delegate* unmanaged[Cdecl]<byte*, int, void> s_Note;

        public static void Note(string message)
        {
            if (s_Note == null)
                s_Note = (delegate* unmanaged[Cdecl]<byte*, int, void>)Host.FindExport("PhysicalTests.Note");
            byte[] bytes = Encoding.UTF8.GetBytes(message);
            fixed (byte* p = bytes)
                s_Note(p, bytes.Length);
        }

        public static string F(float v) => v.ToString("F3", CultureInfo.InvariantCulture);
    }

    // The units and the exposure as C# sees them (the C++ test notes the same line).
    public class CsPhysicalUnits : GameBehaviour
    {
        void Start()
        {
            var eye = new GameObject("UnitsEye");
            Camera cam = eye.AddComponent<Camera>();
            cam.usePhysicalProperties = true;
            cam.aperture = 2.8f;
            cam.shutterSpeed = 1.0f / 60.0f;
            cam.iso = 400;
            var lamp = new GameObject("UnitsLamp");
            Light light = lamp.AddComponent<Light>();
            light.type = LightType.Point;
            light.intensity = 50;
            PhysicalAdditionalLightData data = light.GetPhysicalAdditionalLightData();
            LightUnit before = data.lightUnit;
            float candela = data.intensity;
            data.SetLightUnitAndConvert(LightUnit.Lumen);
            Notes.Note("units:" + before + ":" + Notes.F(candela) + ":" + data.lightUnit + ":" + Notes.F(data.intensity) + ":" +
                       Notes.F(LightUnitUtils.ComputeEV100(cam.aperture, cam.shutterSpeed, cam.iso)) + ":" + Notes.F(LightUnitUtils.Ev100ToNits(3)) + ":" +
                       Notes.F(LightUnitUtils.EV100ToExposure(0)) + ":" + Notes.F(LightUnitUtils.LumenToCandela(1000, LightUnitUtils.SolidAngle(LightType.Point))));
            DestroyImmediate(eye);
            DestroyImmediate(lamp);
        }
    }

    // The test scene of the C++ tests made with the C# API (its golden image is the C++ one's).
    public class CsPhysicalScene : GameBehaviour
    {
        static Mesh Box()
        {
            var v = new List<Vector3>();
            var n = new List<Vector3>();
            var uv = new List<Vector2>();
            var t = new List<int>();
            Vector3[] axes = { new Vector3(1, 0, 0), new Vector3(0, 1, 0), new Vector3(0, 0, 1) };
            for (int a = 0; a < 3; a++)
                foreach (float sign in new[] { 1.0f, -1.0f })
                {
                    Vector3 normal = axes[a] * sign;
                    Vector3 u = axes[(a + 1) % 3] * 0.5f, w = axes[(a + 2) % 3] * 0.5f;
                    if (sign < 0)
                        (u, w) = (w, u);
                    int b = v.Count;
                    Vector3 c = normal * 0.5f;
                    foreach (Vector3 p in new[] { c - u - w, c - u + w, c + u + w, c + u - w })
                    {
                        v.Add(p);
                        n.Add(normal);
                    }
                    uv.Add(new Vector2(0, 0));
                    uv.Add(new Vector2(0, 1));
                    uv.Add(new Vector2(1, 1));
                    uv.Add(new Vector2(1, 0));
                    foreach (int k in new[] { 0, 2, 1, 0, 3, 2 })
                        t.Add(b + k);
                }
            return Made(v, n, uv, t);
        }

        static Mesh Sphere(int rings = 16, int segments = 32)
        {
            var v = new List<Vector3>();
            var n = new List<Vector3>();
            var uv = new List<Vector2>();
            var t = new List<int>();
            for (int r = 0; r <= rings; r++)
                for (int s = 0; s <= segments; s++)
                {
                    float theta = Mathf.PI * r / rings, phi = 2.0f * Mathf.PI * s / segments;
                    var d = new Vector3(Mathf.Sin(theta) * Mathf.Cos(phi), Mathf.Cos(theta), Mathf.Sin(theta) * Mathf.Sin(phi));
                    v.Add(d * 0.5f);
                    n.Add(d);
                    uv.Add(new Vector2((float)s / segments, 1.0f - (float)r / rings));
                }
            for (int r = 0; r < rings; r++)
                for (int s = 0; s < segments; s++)
                {
                    int a = r * (segments + 1) + s, b = a + segments + 1;
                    foreach (int k in new[] { a, a + 1, b, a + 1, b + 1, b })
                        t.Add(k);
                }
            return Made(v, n, uv, t);
        }

        static Mesh Made(List<Vector3> v, List<Vector3> n, List<Vector2> uv, List<int> t)
        {
            var m = new Mesh();
            m.vertices = v.ToArray();
            m.normals = n.ToArray();
            m.uv = uv.ToArray();
            m.triangles = t.ToArray();
            m.RecalculateTangents();
            m.RecalculateBounds();
            return m;
        }

        static Material Opbr(Color baseColor)
        {
            var m = new Material(Shader.Find("Physical/OpenPBR Lite"));
            m.SetColor("_BaseColor", baseColor);
            return m;
        }

        static void Thing(string name, Mesh mesh, Material m, Vector3 position, Vector3 scale)
        {
            var g = new GameObject(name);
            g.transform.position = position;
            g.transform.localScale = scale;
            g.AddComponent<MeshFilter>().sharedMesh = mesh;
            g.AddComponent<MeshRenderer>().sharedMaterial = m;
        }

        void Start()
        {
            var eye = new GameObject("Eye");
            Camera cam = eye.AddComponent<Camera>();
            eye.transform.position = new Vector3(0, 1.6f, -6.5f);
            eye.transform.rotation = Quaternion.Euler(12, 0, 0);
            cam.fieldOfView = 45;
            cam.nearClipPlane = 0.1f;
            cam.farClipPlane = 100;
            cam.clearFlags = CameraClearFlags.SolidColor;
            cam.backgroundColor = new Color(0.02f, 0.03f, 0.06f, 1);

            var sun = new GameObject("Sun");
            Light sunLight = sun.AddComponent<Light>();
            sunLight.type = LightType.Directional;
            sunLight.shadows = LightShadows.Soft;
            sun.transform.rotation = Quaternion.Euler(45, 30, 0);
            PhysicalAdditionalLightData sunData = sunLight.GetPhysicalAdditionalLightData();
            sunData.lightUnit = LightUnit.Lux;
            sunData.intensity = 5000;

            for (int i = 0; i < 8; i++)
            {
                var lamp = new GameObject("Lamp" + i);
                Light l = lamp.AddComponent<Light>();
                l.type = LightType.Point;
                l.range = 4;
                l.color = Color.HSVToRGB(i / 8.0f, 0.8f, 1);
                float angle = 2.0f * Mathf.PI * i / 8.0f;
                lamp.transform.position = new Vector3(Mathf.Cos(angle) * 2.5f, 0.4f, 0.5f + Mathf.Sin(angle) * 2.5f);
                PhysicalAdditionalLightData d = l.GetPhysicalAdditionalLightData();
                d.lightUnit = LightUnit.Lumen;
                d.intensity = 3000;
                d.shapeRadius = 0.05f;
            }
            var spot = new GameObject("Spot");
            Light spotLight = spot.AddComponent<Light>();
            spotLight.type = LightType.Spot;
            spotLight.range = 8;
            spotLight.spotAngle = 50;
            spotLight.innerSpotAngle = 30;
            spotLight.useColorTemperature = true;
            spotLight.colorTemperature = 3000;
            spot.transform.position = new Vector3(0, 3, -1);
            spot.transform.rotation = Quaternion.Euler(60, 0, 0);
            PhysicalAdditionalLightData spotData = spotLight.GetPhysicalAdditionalLightData();
            spotData.lightUnit = LightUnit.Lumen;
            spotData.intensity = 60000;

            Mesh box = Box(), sphere = Sphere();
            Material ground = Opbr(new Color(0.5f, 0.5f, 0.5f, 1));
            ground.SetFloat("_SpecularRoughness", 0.8f);
            Thing("Ground", box, ground, new Vector3(0, -0.05f, 1), new Vector3(8, 0.1f, 8));
            Material red = Opbr(new Color(0.8f, 0.1f, 0.1f, 1));
            red.SetFloat("_SpecularRoughness", 0.4f);
            red.SetFloat("_BaseDiffuseRoughness", 0.5f);
            Thing("Dielectric", sphere, red, new Vector3(-2.1f, 0.6f, 0.5f), new Vector3(1.2f, 1.2f, 1.2f));
            Material gold = Opbr(new Color(1, 0.77f, 0.34f, 1));
            gold.SetFloat("_BaseMetalness", 1);
            gold.SetColor("_SpecularColor", new Color(1, 0.9f, 0.8f, 1));
            gold.SetFloat("_SpecularRoughness", 0.25f);
            Thing("Metal", sphere, gold, new Vector3(-0.7f, 0.6f, 0.5f), new Vector3(1.2f, 1.2f, 1.2f));
            Material coated = Opbr(new Color(0.05f, 0.1f, 0.6f, 1));
            coated.SetFloat("_SpecularRoughness", 0.6f);
            coated.SetFloat("_CoatWeight", 1);
            coated.SetFloat("_CoatRoughness", 0.05f);
            Thing("Coated", sphere, coated, new Vector3(0.7f, 0.6f, 0.5f), new Vector3(1.2f, 1.2f, 1.2f));
            Material silver = Opbr(new Color(0.95f, 0.95f, 0.95f, 1));
            silver.SetFloat("_BaseMetalness", 1);
            silver.SetFloat("_SpecularRoughness", 0.4f);
            silver.SetFloat("_SpecularRoughnessAnisotropy", 0.8f);
            Thing("Anisotropic", sphere, silver, new Vector3(2.1f, 0.6f, 0.5f), new Vector3(1.2f, 1.2f, 1.2f));
            Material glow = Opbr(new Color(0.1f, 0.1f, 0.1f, 1));
            glow.EnableKeyword("_EMISSION");
            glow.SetColor("_EmissionColor", new Color(1, 0.5f, 0.1f, 1));
            glow.SetFloat("_EmissionLuminance", 5000);
            Thing("Emissive", box, glow, new Vector3(0, 0.3f, -1.5f), new Vector3(0.4f, 0.4f, 0.4f));

            var profile = ScriptableObject.CreateInstance<VolumeProfile>();
            var holder = new GameObject("Volume");
            Volume volume = holder.AddComponent<Volume>();
            volume.isGlobal = true;
            volume.sharedProfile = profile;
            Exposure exposure = profile.Add<Exposure>();
            exposure.mode.Override(ExposureMode.Fixed);
            exposure.fixedExposure.Override(10.0f);
            Tonemapping tonemapping = profile.Add<Tonemapping>();
            tonemapping.mode.Override(TonemappingMode.ACES);

            PhysicalRenderPipelineAsset asset = PhysicalRenderPipelineAsset.Create();
            asset.shadowDistance = 20;
            asset.shadowmapResolution = 1024;
            asset.environmentLuminance = 1000;
            asset.maxVisibleLights = 64;
            asset.clusterTileSize = 32;
            GraphicsSettings.defaultRenderPipeline = asset;
            Notes.Note("scene:" + PhysicalRenderPipeline.supportsDevice);
        }
    }
}
