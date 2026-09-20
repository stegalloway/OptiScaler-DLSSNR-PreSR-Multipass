#pragma once
#include <atomic>
namespace DlssNr::NrStabUi
{
enum State : int
{
    NOT_CALLED = 0,
    BYPASSED,
    INELIGIBLE,
    WAIT_MOTION,
    READBACK,
    TESTING,
    READY,
    ACTIVE,
    FAILED
};
inline std::atomic<int> state { NOT_CALLED };
inline std::atomic<int> mvDirection { 0 };
inline std::atomic<bool> historyValid { false };
inline std::atomic<float> motionScaleX { 0.0f };
inline std::atomic<float> motionScaleY { 0.0f };
inline void Publish(State s, int mv, bool history, float sx, float sy)
{
    mvDirection.store(mv, std::memory_order_relaxed);
    historyValid.store(history, std::memory_order_relaxed);
    motionScaleX.store(sx, std::memory_order_relaxed);
    motionScaleY.store(sy, std::memory_order_relaxed);
    state.store((int) s, std::memory_order_release);
}
inline const char* StateName()
{
    switch ((State) state.load(std::memory_order_acquire))
    {
    case BYPASSED:
        return "BYPASSED";
    case INELIGIBLE:
        return "INELIGIBLE";
    case WAIT_MOTION:
        return "WAIT_MOTION";
    case READBACK:
        return "READBACK";
    case TESTING:
        return "TESTING";
    case READY:
        return "READY";
    case ACTIVE:
        return "ACTIVE";
    case FAILED:
        return "FAILED";
    default:
        return "NOT_CALLED";
    }
}
} // namespace DlssNr::NrStabUi
