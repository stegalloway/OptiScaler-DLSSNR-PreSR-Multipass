#pragma once

#include <cstdint>

namespace DlssNr::FinishedPicturePolicy
{
// A successful active DLSS-G evaluation is presentation ownership evidence only
// for the real-frame epoch in which it was observed. A cached interpolation
// count alone must never suppress NR after FG is turned off.
inline constexpr bool FreshDlssgOwnership(int generatedFrames,
                                          uint64_t activeEvaluationEpoch,
                                          uint64_t presentEpoch)
{
    return generatedFrames > 0 && activeEvaluationEpoch == presentEpoch;
}

inline constexpr bool DlssgPresentationOwnership(bool optionsObserved,
                                                 bool optionsActive,
                                                 bool freshEvaluationOwnership)
{
    // Successful Streamline options are authoritative because MFG can produce
    // several Presents from one real evaluation. Use the exact-epoch signal
    // only for direct NGX paths that never submit Streamline options.
    return optionsObserved ? optionsActive : freshEvaluationOwnership;
}

// Ordinary wrapped Present is not the pre-FG handoff. Internal active FG and
// XeFG's app-facing picture have their own ordered call sites. Native/external
// DLSS-G is suppressed here only while runtime presentation ownership is active.
inline constexpr bool AllowWrappedPicture(bool hasQueue, bool xeFgGamePicture,
                                          bool fgExists, bool fgActive,
                                          bool fgPaused, bool freshDlssgOwnership)
{
    return hasQueue && !xeFgGamePicture && !freshDlssgOwnership &&
           (!fgExists || !fgActive || fgPaused);
}

// Cancellation is a lifecycle decision, not a presentation-owner predicate.
// A valid pre-FG handoff must remain usable while DLSS-G owns final display.
inline constexpr bool CancelPending(bool nrEnabled, bool finishedPictureEnabled)
{
    return !nrEnabled || !finishedPictureEnabled;
}

} // namespace DlssNr::FinishedPicturePolicy
