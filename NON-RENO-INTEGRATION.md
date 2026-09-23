# Non-Reno integration candidate — 23 September 2026

Base: `9a2ffbe6`, the Miles-tested v0.8.5 candidate. This is an isolated worktree, not the established main or a game installation. The clean Miles DLL and its recovery copies remain untouched.

## Included already in the base

- v0.8.5 exposure and multi-mip behavior, NRSTAB/ShadowFloor and GUI.
- Downloaded Streamline plugin selection; selected PR #1 NR submission, timing and descriptor-lifetime corrections; exposure held-frame correction.
- Caller-side FG accepted-state, ABI, direct-NGX ownership, Reflex token and command-failure handling. Known v1-v5 game-owned options are forwarded as v5 with per-version field copying. Preserve this tested behavior during every later port.

## Pending, in order of safety dependency

1. Port the *transaction-only* portion of the reviewed FG hardening (`f038f8b0` is a reference, not a wholesale cherry-pick). Establish baseline failure outcomes first. Preserve unique signature families, complete preflight, byte/protection/cache rollback, retained provider ownership and unsafe-failure refusal. Add exclusive DLSS-G export identity before mutation; do not admit DLSS 5 SR or broaden supported profiles from the RTX40MFG archive. Recheck the P5–P8 and T1–T4 requirements against production code.
2. Review ShyVortex PR #9's external-presentation/recursive-lock changes for this host's ownership and apply only relevant corrections. The recursive lock correction is now adapted and tested: a nested Present/Present1 caller skips re-locking only when the same thread owns the mutex. The external finished-picture gate is still pending because v0.8.5 does not expose ShyVortex's external-FG ownership signal; a cached interpolation count could remain stale after FG is turned off. Review PR #7 external-FG cancellation as a separate, ownership-specific policy; do not import it by default into PureDark or game-owned Streamline paths.
3. Investigate exposure scanner caching with provider/module invalidation and feature-recreation tests. Do not cache stale addresses across module replacement.
4. Prepare game-specific variants and acceptance: Miles first; TLOU2/Akane compatibility and water baseline separately; RDR2/PureDark requires its own ownership/epoch adaptation and backup. Do not install this generic candidate into RDR2.

Reno quality modes, Balanced, Warp Blend, and Reno-derived PTX experiments are excluded. The separately parked magnitude-only/native-scatter and Transfusion colour-blending ideas are image-quality experiments, not implied by the reliability integration request. They remain preserved but unported.

## First gate and current evidence

The original `tests/mfg_unlock/run.ps1` on `9a2ffbe6` passes 12 patch cases and the provider, ceiling, PTX, method and flip smoke checks. This establishes a regression baseline, **not** transaction-safety acceptance: the current unlock still mutates kernels before committing both gate patches and lacks the reviewed complete rollback/retained-ownership guarantee.

First isolated production change: exclusive DLSS-G export identity is now checked before `TryApply` latches a provider or touches gates/kernels. The test seam exercises DLSS-G-only acceptance and DirectSR-only, mixed-export, and neither-export refusal; the original 12 patch cases and five MFG helper smokes still pass. Release/x64 RTX40-MFG compilation passes after restoring the nine pinned, hash-verified dependency payloads (5,840 files) to this new worktree. This is the P5 admission guard, **not** T1–T4 transaction completion. The synthetic export seam does not replace a mapped-DLL acceptance test.

Provider retention is now a second narrow addition: failure to acquire a real loader reference refuses mutation; valid references are released on export/signature rejection, and are held once kernel mutation may occur. Synthetic failure/ownership assertions pass and the Release/x64 RTX40-MFG build passes. This addresses the retention precondition of P7 for the one-provider policy, but is **not** complete rollback safety or a proof that the provider bytes/identity cannot change between every validation step. The old mutation helpers still have partial-write failure paths; no game test or promotion is allowed on this checkpoint.

The reviewed `f038f8b0` commit cannot be merged wholesale: it also rewrites the tested v5 game-owned options forwarding and direct-NGX ownership path. An uncommitted trial merge was aborted, leaving the branch clean before the selective P5 change. Transaction code must therefore be adapted separately while preserving the Miles-clean caller path.

A second staged merge confirmed another incompatibility: `f038f8b0` removes the working PTX temporal option, plugin flip-metering control and related UI. That merge was also aborted. The full transaction port must preserve those existing non-Reno capabilities, including rollback across PTX descriptor redirection; the present PTX helper can partially redirect slots and is not yet transaction-safe. PR #9's raw owner-number-only lock bypass would also allow a different thread to skip the mutex. The adapted `OwnedMutex` records the owning thread; its production-backed extracted-header test confirms same-thread recursion and cross-thread blocking. Release/x64 RTX40-MFG build passes. This does not validate PR #9's external finished-picture gate or the remaining transaction work.

Before any game install, require all existing suites plus new transaction failure injection, code review of the v5 options path, an unchanged-known-good hash check, closed game and a verified rollback backup. No main promotion on CPU checks alone.
