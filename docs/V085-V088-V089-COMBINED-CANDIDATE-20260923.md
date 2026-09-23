# v0.8.8 + v0.8.9 combined candidate — 23 September 2026

This is an isolated test build based on the Miles visual-pass safety candidate
`5caf4387` (documentation base `222d7fe0`). It does not replace the established
main, RDR2/PureDark, or TLOU2/Akane builds. The installed Miles control DLL is
preserved separately before any test replacement.

## Included

- v0.8.8 `3f34e024`: optional Transfer 3/4 lighting-and-colour reconstruction.
  Existing Transfer 1 remains the default, so an unchanged INI does not select
  the new reconstruction policy.
- v0.8.9 `9c015f41`: XeFG-owned finished-picture composition ordered on the game
  queue before presentation. This does not claim to change external PureDark FG.
- v0.8.9 `be229250`: optional peripheral spatial compression in DX12 and Vulkan,
  with its mapping, shader, UI and fallback controls. It defaults to **off**.
- The control's NRSTAB, ShadowFloor, exposure, multi-mip, downloaded Streamline
  selection, PR #1 NR safeguards and tested FG transaction/MFG behaviour remain.

Not included: v0.8.91 preview changes, Reno Balanced/Warp Blend or the parked
magnitude-gate experiment. No new game INI is written over an existing one.

## Integration safeguards

- The combined NR codec was rebuilt from the merged shader source with the
  existing 512-byte NRSTAB constant layout. The DX12/DX11 production blob uses
  the FXC-compatible shader target required by the WARP regression fixtures;
  Vulkan uses its separately rebuilt SPIR-V blob.
- Spatial pack/unpack shaders keep their independent 256-byte constant layout.
  The shared 512-byte binding is zero-filled beyond that prefix on both DX12
  and Vulkan. No NRSTAB constants are overread from the smaller spatial struct.
- NRSTAB's base read and copy-back stay on the original full-resolution colour.
  Its motion self-test and selector use the original game motion resource and
  original guide dimensions, never the packed model's motion vectors.
- Switching spatial layout or experiencing an early spatial failure invalidates
  NRSTAB history/readback state. Failed colour encode cannot expose stale
  scratch content. The current frame is not replaced by a partial spatial
  result.

## Validation and limits

The full `tests/run_nr_prerelease.ps1` suite passes, including MFG safety,
Streamline, NR reconstruction, active colour, finished queue, spatial CPU
mapping, production Vulkan spatial shaders and production codec tests. The
Release/x64 RTX40-MFG build compiles and links. These are source, synthetic
and GPU-fixture results, **not** Miles or RDR2 image-quality acceptance.

Miles gate: same scene and FG off/on transition as the visual-pass control;
check for immediate smearing, NRSTAB PATH READY/ACTIVE and MFG unlock. First
keep Transfer 1 and spatial compression off to detect a default-path
regression. Only after that passes, test Transfer 3 and spatial compression
one at a time. If any test fails, save the log, close the game and restore the
exact control DLL. RDR2/PureDark and TLOU2 each require separate game tests;
the XeFG route is not validated merely by a Miles DLSS-G pass.

Source branch: `candidate/v085-reconstruction-spatial-20260923`, worktree
`GitHub/02_WORKTREES/V085-Reconstruction-Spatial-20260923`.

## Miles result — post-build documentation

The user reported "all good" after the Miles session on 23 September 2026.
The `9023e881` DLL remains installed and its hash is unchanged. The game closed
normally; the preserved final log shows MFG's complete PTX transaction,
NRSTAB PATH READY and ACTIVE, FG off/on, and an orderly GPU-owner drain. The
final NRSTAB STATUS recorded 7,399 selector frames and 7,388 warm-history
frames. No device removal, incomplete rollback or rejected DLSSG options were
found. The exact backup and result paths are in the build candidate's
`TEST-MANIFEST.md` under `04_BUILDS/CANDIDATES`.

The tested INI has `WorkingScale=0.8`, `SpatialCompression=true` and
`ColourStrength=0`; the 2420x1038 model extent agrees with the optional 90%
spatial working layout at that scale. This is evidence of the combined spatial
configuration during the session, not a controlled image-quality comparison.
`Transfer=auto` stayed on matched residual in that first session; separate
Transfer 3/4 trials and their limits are recorded below. Miles DLSS-G
does not validate the XeFG game-queue path or RDR2/PureDark ownership. This
documentation-only update does not change the tested `9023e881` binary.

## XeFG and RDR2 ownership audit — 23 September 2026

The v0.8.9 finished-picture route is conditional on OptiScaler owning XeFG's
app-facing swapchain (`activeFgOutput == XeFG`, no swapchain interop, and the
presented swapchain matching `currentFGSwapchain`). In that route, NR composition
uses XeFG's game command queue before the provider's Present; the wrapped
swapchain path avoids composing the same picture again. The WARP finished-queue
fixture passed, but it proves ordering/readiness in a synthetic queue test, not
visual behaviour with a live XeFG provider.

The installed RDR2 module remains the separate PureDark coexistence
`OptiScaler.asi`, SHA-256
`1825A7F30350361F312D97B50FE476CDA147F6ADC8ADC083FD3DC30B716491CC`.
Its latest preserved log (20 September) explicitly records PureDark ownership
of DXGI, Streamline, Reflex and FG, with OptiScaler retaining NGX SR/NR. The
currently installed PureDark DLSS-G provider reports 310.9.1; the older
19 September handoff's 310.1.0 provider warning is historical, not current
inventory. The provider update does not change presentation ownership.

Therefore neither the Miles DLSS-G session nor this RDR2/PureDark installation
can validate the XeFG-specific v0.8.9 path. No generic DLL, RDR2 ASI, settings
or runtime files were changed for this audit. XeFG runtime validation remains
open for a separate OptiScaler-owned XeFG target; it is not a reason to risk
the working RDR2 installation.

## Miles Transfer 3 checkpoint — no-smearing report

With the same `9023e881` DLL and spatial compression still enabled at 0.8
working scale, the game INI was backed up and only `Transfer=auto` was changed
to `Transfer=3` before launch. The log confirms "transfer lighting + colour",
NRSTAB PATH READY/ACTIVE, a DLSS-G off/on transition, and normal shutdown.
`ColourStrength=0` remained, so this probes the lighting/composition route but
does not establish the full colour-treatment benefit. During gameplay the FG
interpolation override changed from 3 to 2; the saved final INI differs from
the pretest control in those two lines. The user confirmed the multiplier
change was intentional and reported no smearing. This is an
operational/stability observation, **not** a one-variable image-quality A/B.
The exact final log and INI are preserved under
`06_TEST-RESULTS/V085-V088-V089-COMBINED-9023E881-20260923/MILES-TRANSFER3-20260923`.

The Transfer 3 log is 856,855 bytes, SHA-256
`C667B18900552DD3BDF585C2C9DF626D838747CDB32304760691D3D4F4B6B9BE`;
the tested INI is SHA-256
`F35C4A050C7DCCD6E70628EEF83D37548F942A5B0B4648D62DE025E0C8A9EDC7`.
The user observation, not telemetry alone, establishes the no-smearing verdict.

Transfer 4 then started from this exact saved state: the DLL and the 2-frame
interpolation override remained unchanged, the DLL/INI/log were verified in
`05_BACKUPS/MILES-BEFORE-TRANSFER4-9023E881-20260923`, and only `Transfer=3`
became `Transfer=4`. The log confirms creation of private DLSS SR at
2688x1152 to 3360x1440, successful evaluation on the producer queue, NRSTAB
PATH READY/ACTIVE, and FG off/on transitions. No device removal, incomplete
rollback, or failed first private enlargement evaluation was found. The user
reported **no smearing or other visual regression**. This validates the tested
Transfer 4 runtime path in Miles, not its image-quality superiority or XeFG.
The final log (872,742 bytes) is SHA-256
`AFAF4B1AC5DDBE453B53A24EA5797D52CAFDA48E5466DBFD196F48CFDFF02A45`;
the exact tested INI is SHA-256
`E3E619E8E488DD7C908FC747556FCD7CC208A084BC9C600D21158C03913AABAE`.
Both are saved under
`06_TEST-RESULTS/V085-V088-V089-COMBINED-9023E881-20260923/MILES-TRANSFER4-20260923`.

After that test closed, only `Transfer=4` was returned to `Transfer=auto` in
the game INI. The user's intentional interpolation override of 2 and all
other settings remain. The installed candidate DLL remains `9023e881` at
SHA-256 `E293011B287A5B0B7C13514FD05D728A7D85FCEF11686CEEAD1F794BA1A3A342`.
No source or RDR2/TLOU2 game install was changed by these runtime tests.
