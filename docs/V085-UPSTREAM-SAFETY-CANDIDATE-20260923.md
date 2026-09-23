# v0.8.5 non-Reno + upstream safety candidate — 23 September 2026

This is an isolated, additive test candidate. Its control is the Miles visual-pass build at `85fca719`, not the older canonical `d3fcc16f` main. The control DLL remains backed up; game settings, RDR2 build, and TLOU2 build were not changed.

## Included

- Upstream v0.8.6 `dac290ed`: NGX shutdown ordering and idempotence; NR GPU-owner retirement; failure/device-removal handling; process-detach guards. It changes lifetime/teardown rather than NR shader policy or MFG patching.
- Upstream v0.8.7 `dd2b7906`: finished-picture cross-queue safety. Unready cross-queue input is skipped instead of making presentation wait on a possibly presentation-dependent producer. This can skip optional finished-picture NR frames; pre-SR NR is unaffected.
- Local merge adaptations: preserve the control's pre-submission token capture, descriptor-slot ownership, late producer lifetimes, and readback failure tests. Shutdown may close only a genuinely completed submitted recording; it must not release unsent, blocked, failed-signal or removed-device work. The production-backed shutdown harness also checks retirement of a locally owned finished command list.
- Build-path-only correction: the resource metadata pre-build command now resolves a workspace path with spaces; use `PostBuildEventUseInBuild=false` because the upstream post-build copy script is not space-safe. Package this candidate separately instead of running that script.

## Explicitly excluded

No v0.8.8 reconstruction, v0.8.9 XeFG/spatial-compression changes, v0.8.91 preview, Reno modes or Warp Blend. The control's NRSTAB, ShadowFloor, exposure, multi-mip, downloaded Streamline selection, PR #1 NR protections and validated FG transaction work stay in place; this candidate does not re-port or retune them.

## Gates and evidence

- Full `tests/run_nr_prerelease.ps1`: pass, including shutdown, GPU lifetime, proxy, MFG, Streamline, WARP shader/copy and Vulkan production-shader checks.
- Release/x64 `OptiScalerRtx40Mfg=true` compile and link: pass. Existing unrelated compiler/linker warnings remain. The old post-build copy step failed on spaces; repeating the build with `PostBuildEventUseInBuild=false` exited cleanly and produced the DLL.
- `git diff --check`: pass. No game or NVIDIA runtime test is implied by CPU/WARP success.
- Miles game test: **visual pass**, user report on 23 September 2026: "all good". The tested DLL is the `5caf4387` Release/x64 RTX40-MFG binary, SHA-256 `4B2D57E9C387F1312285146A96AC4D1E261172C5BCC69ED3FA888C5E3DD2023E`. This is not a promotion to main or a cross-game result.

## Miles session evidence

The log confirms marker `5caf4387`, completed PTX descriptor/gate MFG patching, Streamline DLSS-G unloading then loading again, and NRSTAB PATH READY followed by ACTIVE with K=1, a 2-pixel motion gate and valid history. Its final STATUS has 5,766 selector frames and 5,756 warm-history frames. There are no device-removal, DLSSG-options-rejected, evaluation-refused or incomplete-rollback messages. On exit, DX12 shutdown logged NR retirement, completion of the drain, then NVIDIA runtime shutdown; the game process closed.

The session does still contain 4 Streamline `common`-constants multiple-set errors, one missing-constants fallback, 10 startup manifest-parse errors, and a flip-metering warning when 3X was requested. These are not described as a clean log or as fixed by this candidate; the earlier Miles control session also had the same classes of Streamline errors. Final preserved log: `06_TEST-RESULTS/V085-UPSTREAM-SAFETY-5CAF4387-20260923/Miles-20260923-visual-pass-final-OptiScaler.log`, 966,584 bytes, SHA-256 `C89995E627E8CF4F11106AB73604A1B16FAA81615CFE018D917A362DE09D4857`.

This does not exercise v0.8.7's optional finished-picture route: the Miles STATUS records `finished_picture=0`. Its cross-queue safety remains WARP-tested but not game-validated here. The shutdown result is a normal exit, not device-removal or abnormal-termination validation.

## Rollback and interpretation

Back up the exact installed `dxgi.dll` and INI before replacement. The control `85fca719` DLL has SHA-256 `0D22779FF080959E5683F0621B6133A2E6A2B370E8A3E29871AD8EE87E7C63E9`. If the candidate crashes, smears or loses MFG, close Miles, preserve `OptiScaler.log`, and restore that control DLL. Do not touch the INI for this comparison. A clean Miles result does not certify RDR2/PureDark or TLOU2; v0.8.7 can deliberately skip unfinished cross-queue finished-picture NR, which is especially relevant to RDR2.
