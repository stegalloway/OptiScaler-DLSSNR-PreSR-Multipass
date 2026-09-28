#include <cassert>
#include <iostream>
#include <vector>
#include "../OptiScaler/shaders/dlssnr/DlssNr_Rdr2Clock.h"

int main()
{
    DlssNrRdr2Clock c;
    int a = 0, b = 0, unrelated = 0;
    int* one[] { &a };
    int* two[] { &a, &b };
    int* other[] { &unrelated };
    assert(c.Register(nullptr) == 0);
    assert(c.ScanTick(nullptr) == 0);
    assert(c.Submitted(1, static_cast<int* const*>(nullptr)) == 0);
    assert(c.Register(&a) == 0);
    assert(c.Register(&a) == 0);
    assert(c.ScanTick(&a) == 1 && c.ScanTick(&a) == 1);
    assert(c.Submitted(1, other) == 0);
    c.ResetRecording(&a, false);
    assert(c.ScanTick(&a) == 1);
    assert(c.Submitted(1, one) == 1);
    assert(c.Submitted(1, one) == 0); // replay is not another registered NR submission
    assert(c.Register(&a) == 1);
    c.ResetRecording(&a, true); // discard unsubmitted NR and reuse the pointer
    assert(c.Submitted(1, one) == 0);
    assert(c.ScanTick(&a) == 2); // scan still progresses after the drop
    assert(c.Register(&a) == 1 && c.Register(&b) == 1);
    assert(c.Submitted(2, two) == 2); // only one epoch per batch
    assert(c.Submitted(0, two) == 0);
    c.ResetRecording(&a, true); // Reset tracking remains active while NR is off
    c.ResetRecording(&a, true);
    assert(c.ScanTick(&a) == 3);
    assert(c.Register(&a) == 2 && c.Submitted(1, one) == 3);
    assert(c.ScanTick(&b) == 4 && c.ScanTick(&b) == 4);
    assert(c.ScanTick(&a) == 5); // source cffccbca last-(list,generation) contract
    c.Clear();
    assert(c.Register(nullptr) == 0 && c.ScanTick(&a) == 1);
    std::vector<int> many(257);
    for (auto& x : many)
        c.Register(&x);
    assert(c.Overflows() == 1);
    int* expired[] { &many[0] };
    int* retained[] { &many.back() };
    assert(c.Submitted(1, expired) == 0 && c.Submitted(1, retained) == 1);
    std::cout << "PASS RDR2 production clock: dedup, failed/successful Reset, discarded/reused recordings, "
                 "scan-after-drop, unrelated/replayed submissions, batch epoch, toggle lifetime, cap and clear\n";
}
