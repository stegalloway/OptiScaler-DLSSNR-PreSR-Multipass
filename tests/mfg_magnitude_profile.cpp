#include <cassert>
#include <cstdio>
#include <cstring>

#include "../OptiScaler/framegen/dlssg/MfgMagnitudeProfile.h"

int main()
{
    using namespace MfgMagnitude;

    static_assert(kDefaultDisplayThresholdPx == 64);
    static_assert(kDisplayThresholdsPx.size() == 4);
    static_assert(kCalibrationOutputWidth == 3440 && kCalibrationOutputHeight == 1440);
    static_assert(kCalibrationMotionGridWidth == 1720 && kCalibrationMotionGridHeight == 720);
    static_assert(kDisplayPixelsPerGridUnit == 2.0f);

    assert(IsValidDisplayThresholdPx(16));
    assert(IsValidDisplayThresholdPx(32));
    assert(IsValidDisplayThresholdPx(48));
    assert(IsValidDisplayThresholdPx(64));
    assert(!IsValidDisplayThresholdPx(0));
    assert(!IsValidDisplayThresholdPx(24));
    assert(!IsValidDisplayThresholdPx(96));

    assert(std::strcmp(MechanismForDisplayThresholdPx(16), "magnitude_display_16px") == 0);
    assert(std::strcmp(MechanismForDisplayThresholdPx(32), "magnitude_display_32px") == 0);
    assert(std::strcmp(MechanismForDisplayThresholdPx(48), "magnitude_display_48px") == 0);
    assert(std::strcmp(MechanismForDisplayThresholdPx(64), "magnitude_display_64px") == 0);
    assert(MechanismForDisplayThresholdPx(24) == nullptr);

    std::puts("PASS: calibrated magnitude display-threshold profile contract");
    return 0;
}
