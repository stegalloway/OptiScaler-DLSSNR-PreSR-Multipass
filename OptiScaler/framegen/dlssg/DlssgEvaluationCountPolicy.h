#pragma once

#include <algorithm>
#include <optional>

// Direct NGX can apply a positive OptiScaler count, but zero (Off) cannot be
// expressed by rewriting a feature evaluation. Native counts belong to the game.
inline int ResolveDlssgEvaluationFrameCount(int nativeCount, const std::optional<int>& overrideCount,
                                            int verifiedMaximum)
{
    if (!overrideCount.has_value() || overrideCount.value() <= 0)
        return nativeCount;
    return std::clamp(overrideCount.value(), 1, std::max(1, verifiedMaximum));
}
