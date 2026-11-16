using System;

namespace HelixEngine.Rendering.Physical
{
    /// <summary>
    /// The Physical pipeline. The engine makes and runs it in C++ while a PhysicalRenderPipelineAsset is set; this C#
    /// object is RenderPipelineManager.currentPipeline then (made by the asset's CreatePipeline, disposed with the C++
    /// one), and C# reads the pipeline's constants and asset here.
    /// </summary>
    public sealed partial class PhysicalRenderPipeline : RenderPipeline
    {
        /// <summary>The value of the RenderPipeline tag of the SubShaders it draws.</summary>
        public const string k_ShaderTagName = "PhysicalPipeline";

        readonly PhysicalRenderPipelineAsset m_Asset;

        internal PhysicalRenderPipeline(PhysicalRenderPipelineAsset asset) => m_Asset = asset;

        /// <summary>The pipeline asset in use when it is this pipeline's (else null).</summary>
        public static PhysicalRenderPipelineAsset asset => PhysicalRenderPipelineNative.Asset();

        /// <summary>Whether the pipeline draws on the graphics device (compute shaders and storage buffers in every stage).</summary>
        public static bool supportsDevice => PhysicalRenderPipelineNative.SupportsDevice();

        /// <summary>What the last camera's frame drew.</summary>
        internal static PhysicalFrameInfo lastFrame => PhysicalRenderPipelineNative.LastFrame();

        /// <summary>Runs in C++ (the engine renders with the native pipeline).</summary>
        protected override void Render(ScriptableRenderContext context, Camera[] cameras) =>
            throw new InvalidOperationException("The Physical pipeline renders in C++.");

        public override string ToString() => m_Asset != null ? $"PhysicalRenderPipeline ({m_Asset.name})" : "PhysicalRenderPipeline";
    }
}
