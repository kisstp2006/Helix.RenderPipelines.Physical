using System;

namespace HelixEngine.Rendering.Physical
{
    // What the asset's C# class adds to its bindings: RenderPipelineAsset's members (the engine renders with the C++
    // pipeline; CreatePipeline makes its C# view, RenderPipelineManager.currentPipeline).
    public partial class PhysicalRenderPipelineAsset
    {
        /// <summary>The C# pipeline of the C++ one the engine renders with: RenderPipelineManager.currentPipeline.</summary>
        protected override RenderPipeline CreatePipeline() => new PhysicalRenderPipeline(this);

        public override Type pipelineType => typeof(PhysicalRenderPipeline);

        public override Material defaultMaterial => defaultMaterialNative;

        public override Shader defaultShader => defaultShaderNative;

        public override string renderPipelineShaderTag => PhysicalRenderPipeline.k_ShaderTagName;
    }
}
