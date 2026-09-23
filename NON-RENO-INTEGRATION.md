# Non-Reno integration candidate — 23 September 2026

Base: `9a2ffbe6`, the Miles-tested v0.8.5 candidate. This is an isolated worktree, not the established main or a game installation. The clean Miles DLL and its recovery copies remain untouched.

## Included already in the base

- v0.8.5 exposure and multi-mip behavior, NRSTAB/ShadowFloor and GUI.
- Downloaded Streamline plugin selection; selected PR #1 NR submission, timing and descriptor-lifetime corrections; exposure held-frame correction.
- Caller-side FG accepted-state, ABI, direct-NGX ownership, Reflex token and command-failure handling. Known v1-v5 game-owned options are forwarded as v5 with per-version field copying. Preserve this tested behavior during every later port.

## Pending, in order of safety dependency

1. Port the *transaction-only* portion of the reviewed FG hardening (`f038f8b0` is a reference, not a wholesale cherry-pick). Establish baseline failure outcomes first. Preserve unique signature families, complete preflight, byte/protection/cache rollback, retained provider ownership and unsafe-failure refusal. Add exclusive DLSS-G export identity before mutation; do not admit DLSS 5 SR or broaden supported profiles from the RTX40MFG archive. Recheck the P5–P8 and T1–T4 requirements against production code.
2. Review ShyVortex PR #9's external-presentation/recursive-lock changes for this host's ownership and apply only relevant corrections. Review PR #7 external-FG cancellation as a separate, ownership-specific policy; do not import it by default into PureDark or game-owned Streamline paths.
3. Investigate exposure scanner caching with provider/module invalidation and feature-recreation tests. Do not cache stale addresses across module replacement.
4. Prepare game-specific variants and acceptance: Miles first; TLOU2/Akane compatibility and water baseline separately; RDR2/PureDark requires its own ownership/epoch adaptation and backup. Do not install this generic candidate into RDR2.

Reno quality modes, Balanced, Warp Blend, and Reno-derived PTX experiments are excluded. The separately parked magnitude-only/native-scatter and Transfusion colour-blending ideas are image-quality experiments, not implied by the reliability integration request. They remain preserved but unported.

## First gate and current evidence

The original `tests/mfg_unlock/run.ps1` on `9a2ffbe6` passes 12 patch cases and the provider, ceiling, PTX, method and flip smoke checks. This establishes a regression baseline, **not** transaction-safety acceptance: the current unlock still mutates kernels before committing both gate patches and lacks the reviewed complete rollback/retained-ownership guarantee.

No production source has been changed in this worktree yet. Before any game install, require Release/x64 build, all existing suites plus new failure injection, code review of the v5 options path, an unchanged-known-good hash check, closed game and a verified rollback backup. No main promotion on CPU checks alone.
