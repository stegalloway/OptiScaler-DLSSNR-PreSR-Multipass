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
`Transfer=auto` stayed on matched residual, so the new v0.8.8 Transfer 3/4
lighting-and-colour modes still need a separate visual trial. Miles DLSS-G
does not validate the XeFG game-queue path or RDR2/PureDark ownership. This
documentation-only update does not change the tested `9023e881` binary.
