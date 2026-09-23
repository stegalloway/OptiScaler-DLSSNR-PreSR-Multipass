# v0.8.5 non-Reno + upstream safety candidate — 23 September 2026

This is an isolated, additive test candidate. Its control is the Miles visual-pass build at `85fca719`, not the older canonical `d3fcc16f` main. The control DLL, game settings, RDR2 build, and TLOU2 build are not modified by source work here.

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
- Miles game test: **pending** until the packaged DLL is installed and the user checks the same immediate-smear scene, FG off/on, NRSTAB status and MFG/PTX operation. Record the log and result before any promotion.

## Rollback and interpretation

Back up the exact installed `dxgi.dll` and INI before replacement. The control `85fca719` DLL has SHA-256 `0D22779FF080959E5683F0621B6133A2E6A2B370E8A3E29871AD8EE87E7C63E9`. If the candidate crashes, smears or loses MFG, close Miles, preserve `OptiScaler.log`, and restore that control DLL. Do not touch the INI for this comparison. A clean Miles result does not certify RDR2/PureDark or TLOU2; v0.8.7 can deliberately skip unfinished cross-queue finished-picture NR, which is especially relevant to RDR2.
