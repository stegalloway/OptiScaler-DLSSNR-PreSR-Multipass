#pragma once

#include <array>

namespace MfgMagnitude
{
inline constexpr std::array<int, 4> kDisplayThresholdsPx {16, 32, 48, 64};
inline constexpr int kDefaultDisplayThresholdPx = 64;
inline constexpr int kCalibrationOutputWidth = 3440;
inline constexpr int kCalibrationOutputHeight = 1440;
inline constexpr int kCalibrationMotionGridWidth = 1720;
inline constexpr int kCalibrationMotionGridHeight = 720;
inline constexpr float kDisplayPixelsPerGridUnit = 2.0f;

inline constexpr bool IsValidDisplayThresholdPx(int px)
{
    for (const int candidate : kDisplayThresholdsPx)
        if (candidate == px)
            return true;
    return false;
}

inline constexpr const char* MechanismForDisplayThresholdPx(int px)
{
    switch (px)
    {
    case 16: return "magnitude_display_16px";
    case 32: return "magnitude_display_32px";
    case 48: return "magnitude_display_48px";
    case 64: return "magnitude_display_64px";
    default: return nullptr;
    }
}
} // namespace MfgMagnitude
