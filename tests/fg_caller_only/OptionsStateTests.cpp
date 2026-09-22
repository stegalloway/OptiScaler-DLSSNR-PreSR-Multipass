#include "../../OptiScaler/hooks/DlssgOptionsState.h"
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
    return 0;
}
