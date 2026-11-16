using System;

namespace HelixEngine.Rendering.Physical
{
    // The C# side of the pipeline's volume components: their parameters are C++ members (the generated properties), the
    // enum parameters' C# classes are here.

    [Serializable]
    public sealed class ExposureModeParameter : VolumeParameter<ExposureMode>
    {
        public ExposureModeParameter(ExposureMode value, bool overrideState = false) : base(value, overrideState) { }
    }

    [Serializable]
    public sealed class AdaptationModeParameter : VolumeParameter<AdaptationMode>
    {
        public AdaptationModeParameter(AdaptationMode value, bool overrideState = false) : base(value, overrideState) { }
    }

    [Serializable]
    public sealed class TonemappingModeParameter : VolumeParameter<TonemappingMode>
    {
        public TonemappingModeParameter(TonemappingMode value, bool overrideState = false) : base(value, overrideState) { }
    }

    public sealed partial class Tonemapping : IPostProcessComponent
    {
    }
}
