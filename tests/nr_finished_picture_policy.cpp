#include <cassert>
#include <cstdint>
#include <cstdio>

#include "../OptiScaler/dlssnr/DlssNr_FinishedPicturePolicy.h"

using namespace DlssNr::FinishedPicturePolicy;

int main()
{
    // Direct-NGX fallback: only a fresh successful active evaluation owns.
    assert(FreshDlssgOwnership(1, 42, 42));
    assert(FreshDlssgOwnership(5, 42, 42));
    assert(!FreshDlssgOwnership(0, 42, 42));
    assert(!FreshDlssgOwnership(5, 41, 42));
    assert(!FreshDlssgOwnership(5, uint64_t(-1), 42));

    // Before Streamline feature-state evidence exists, successful options own.
    assert(DlssgPresentationOwnership(true, true, false, false, false));

    // Feature loaded is authoritative across internal interpolation pauses.
    assert(DlssgPresentationOwnership(true, true, true, true, false));

    // Miles keeps SetOptions eOn when the actual DLSS-G feature is unloaded.
    // Feature unload must therefore release ownership despite stale options/eval.
    assert(!DlssgPresentationOwnership(true, true, true, false, true));

    // Feature state can also establish ownership before any SetOptions call.
    assert(DlssgPresentationOwnership(false, false, true, true, false));
    assert(!DlssgPresentationOwnership(false, false, true, false, true));

    // A real successful SetOptions Off remains an immediate release.
    assert(!DlssgPresentationOwnership(true, false, true, true, true));

    // If no Streamline evidence exists, direct-NGX evaluation is the fallback.
    assert(DlssgPresentationOwnership(false, false, false, false, true));
    assert(!DlssgPresentationOwnership(false, false, false, false, false));

    // A live direct-NVNGX evaluation remains authoritative even if an unrelated
    // Streamline feature-state observation says unloaded. Provider selection
    // without a fresh evaluation is not ownership.
    assert(RuntimeDlssgPresentationOwnership(false, true, true));
    assert(!RuntimeDlssgPresentationOwnership(false, true, false));
    assert(!RuntimeDlssgPresentationOwnership(false, false, true));
    assert(RuntimeDlssgPresentationOwnership(true, false, false));

    // D3D11 follows the same presentation-owner rule without requiring a D3D12 queue.
    assert(AllowWrappedPictureDx11(false, false, false, false));
    assert(!AllowWrappedPictureDx11(false, false, false, true));
    assert(!AllowWrappedPictureDx11(true, true, false, false));
    assert(AllowWrappedPictureDx11(true, true, true, false));

    // Normal non-FG path remains eligible.
    assert(AllowWrappedPicture(true, false, false, false, false, false));

    // External/native DLSS-G ownership suppresses ordinary wrapped finished picture.
    assert(!AllowWrappedPicture(true, false, false, false, false, true));

    // Internal active FG and XeFG app-picture routing remain separate.
    assert(!AllowWrappedPicture(true, false, true, true, false, false));
    assert(!AllowWrappedPicture(true, true, true, false, true, false));

    // A paused/inactive internal provider returns ownership to wrapped Present.
    assert(AllowWrappedPicture(true, false, true, false, false, false));
    assert(AllowWrappedPicture(true, false, true, true, true, false));

    // Cancellation remains deliberately independent from presentation ownership.
    assert(!CancelPending(true, true));
    assert(CancelPending(false, true));
    assert(CancelPending(true, false));
    assert(CancelPending(false, false));

    std::puts("PASS: DLSSG feature-state ownership, options/direct fallback and cancellation separation");
    return 0;
}
