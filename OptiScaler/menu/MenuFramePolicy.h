#pragma once

namespace MenuFramePolicy
{
// An outline remains visible after the settings menu closes. It needs its own
// ImGui frame even when the FPS overlay and comparison labels are disabled.
constexpr bool SpatialOutlinesNeedFrame(bool nrEnabled, bool compressionEnabled, bool showCenter, bool showWork)
{
    return nrEnabled && compressionEnabled && (showCenter || showWork);
}
} // namespace MenuFramePolicy
