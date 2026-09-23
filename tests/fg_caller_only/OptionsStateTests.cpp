#include "../../OptiScaler/hooks/DlssgOptionsState.h"
#include "../../OptiScaler/framegen/dlssg/DlssgEvaluationCountPolicy.h"
#include <cassert>

int main()
{
    DlssgOptionsState state;
    state.Initialize({2, false, std::nullopt});
    assert(state.Read().values.generatedFrames == 2);
    state.Queue({3, false, std::nullopt});
    const auto first = state.Read();
    assert(state.Pending() && first.values.generatedFrames == 3);
    state.Queue({4, true, 120.0f});
    state.Accepted(first.generation); // Stale runtime acceptance cannot consume a newer UI edit.
    assert(state.Pending());
    const auto second = state.Read();
    assert(second.values.generatedFrames == 4 && second.values.forceDynamic);
    state.Accepted(second.generation);
    assert(!state.Pending());

    // Game-owned/native evaluation is not normalised, including zero and a
    // legitimate native count above our cached maximum.
    assert(ResolveDlssgEvaluationFrameCount(0, std::nullopt, 1) == 0);
    assert(ResolveDlssgEvaluationFrameCount(5, std::nullopt, 1) == 5);
    // Direct NGX cannot express the menu's Off request by changing a count.
    assert(ResolveDlssgEvaluationFrameCount(2, 0, 5) == 2);
    assert(ResolveDlssgEvaluationFrameCount(2, -1, 5) == 2);
    // Only positive OptiScaler overrides are bounded to verified capability.
    assert(ResolveDlssgEvaluationFrameCount(1, 4, 2) == 2);
    assert(ResolveDlssgEvaluationFrameCount(1, 2, 5) == 2);
    return 0;
}
