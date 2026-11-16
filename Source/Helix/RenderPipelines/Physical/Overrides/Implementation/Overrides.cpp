#include <Helix/RenderPipelines/Physical/Overrides/Overrides.h>

namespace helix::rendering::physical
{
    Exposure::Exposure()
    {
        SetDisplayName("Exposure");
        AddParameter(mode);
        AddParameter(fixedExposure);
        AddParameter(compensation);
        AddParameter(limitMin);
        AddParameter(limitMax);
        AddParameter(histogramLowPercentage);
        AddParameter(histogramHighPercentage);
        AddParameter(adaptationMode);
        AddParameter(adaptationSpeedDarkToLight);
        AddParameter(adaptationSpeedLightToDark);
    }

    Tonemapping::Tonemapping()
    {
        SetDisplayName("Tonemapping");
        AddParameter(mode);
    }
}
