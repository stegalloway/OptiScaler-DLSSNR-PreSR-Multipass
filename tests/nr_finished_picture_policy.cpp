#include <cassert>
#include <cstdint>
#include <cstdio>

#include "../OptiScaler/dlssnr/DlssNr_FinishedPicturePolicy.h"

using namespace DlssNr::FinishedPicturePolicy;

int main()
{
    // Fresh successful DLSS-G evaluation owns this real frame.
    assert(FreshDlssgOwnership(1, 42, 42));
    assert(FreshDlssgOwnership(5, 42, 42));
    assert(!FreshDlssgOwnership(0, 42, 42));

    // Cached counts expire at the next wrapped Present epoch.
    assert(!FreshDlssgOwnership(5, 41, 42));
    assert(!FreshDlssgOwnership(5, uint64_t(-1), 42));

    // Normal non-FG path remains eligible.
    assert(AllowWrappedPicture(true, false, false, false, false, false));

    // Internal active FG and XeFG app-picture routing remain separate.
    assert(!AllowWrappedPicture(true, false, true, true, false, false));
    assert(!AllowWrappedPicture(true, true, true, false, true, false));

    // A paused/inactive internal provider returns ownership to wrapped Present.
    assert(AllowWrappedPicture(true, false, true, false, false, false));
    assert(AllowWrappedPicture(true, false, true, true, true, false));

    // Native/external DLSS-G needs no OptiScaler FG object, but fresh runtime
    // evidence suppresses only the ordinary post-provider picture.
    assert(!AllowWrappedPicture(true, false, false, false, false, true));

    // FG-off recovery is immediate once the evaluation epoch becomes stale.
    assert(AllowWrappedPicture(true, false, false, false, false, false));

    // Cancellation is deliberately independent from FG ownership.
    assert(!CancelPending(true, true));
    assert(CancelPending(false, true));
    assert(CancelPending(true, false));
    assert(CancelPending(false, false));

    std::puts("PASS: finished-picture ownership freshness and cancellation separation");
    return 0;
}
