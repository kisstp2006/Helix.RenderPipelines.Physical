namespace HelixEngine.Rendering.Physical
{
    public partial class PhysicalAdditionalCameraData
    {
        /// <summary>The layers of the volumes blended for this camera.</summary>
        public LayerMask volumeLayerMask
        {
            get => volumeLayerMaskBits;
            set => volumeLayerMaskBits = value;
        }
    }

    /// <summary>The pipeline's settings of a camera: the component beside it (added when it has none).</summary>
    public static class PhysicalCameraExtensions
    {
        public static PhysicalAdditionalCameraData GetPhysicalAdditionalCameraData(this Camera camera) => PhysicalAdditionalDataNative.OfCamera(camera);
    }

    /// <summary>The pipeline's settings of a light: the component beside it (added when it has none, with the light's
    /// intensity in its type's default unit).</summary>
    public static class PhysicalLightExtensions
    {
        public static PhysicalAdditionalLightData GetPhysicalAdditionalLightData(this Light light) => PhysicalAdditionalDataNative.OfLight(light);
    }
}
