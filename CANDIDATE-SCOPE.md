# FG caller-state-only Miles candidate (2026-09-22)

Base: `36ebf6e4649f4e32ad1034777cb19ada92b35d22`, the working v0.8.5 NR/exposure line.

This candidate ports caller-side FG acceptance/failure handling: Streamline options intent and ABI handling, accepted-state updates only on runtime success, direct NGX override ownership, Reflex frame-token fallback, command-list/allocator failure refusal, and null command-list admission checks. It does not include ShyVortex PR #9 presentation changes.

The working PTX unlock remains the baseline code: `MfgUnlock.cpp`, `MfgUnlock.h`, `MfgUnlockPtx.h`, provider selection, plugin-ceiling and flip-metering implementation, and the Ada unlock UI are unchanged. Callers use `UnlockedMax()` from that implementation. Transaction-specific refusal and rollback logic from the broader hardening commit is **not** claimed or tested here.

Build and CPU checks cannot establish game image quality. This is a reversible Miles A/B test only; do not promote to main or use as a general release without gameplay validation. Compare against the saved working DLL with the same settings and scene, including FG off→on and PTX MFG status.
