# TLOU2/Akane candidate acceptance, frozen before production edit

Date: 24 September 2026. Isolated branch:
`candidate/tlou2-v085-akane-20260924`, forked at Miles documentation commit
`1e397624`. The inherited production state is `2c213e50` (the Miles
native-2x visual-pass, downloaded-Streamline-plugin candidate). The installed
Miles DLL, protected `nrstab-presr-v1` checkout, and RDR2 remain untouched.

## Evidence and scope

TLOU2 currently loads `OptiScaler.asi` through its ASI loader, not Miles's
`dxgi.dll`. The 20 September TLOU2 log identifies an older `0ab660b9` DLL
and reports `Akane compatibility: DirectComposition passthrough 720x1000`.
The Akane log independently reports renderer initialization at 720x1000.
The old TLOU2 source commit `3f70c642` introduced only an exact-size,
`tlou-ii.exe`-scoped pass-through in `CreateSwapChainForComposition`.

The v0.8.5-lineage Miles source otherwise wraps valid composition swapchains
for plain presentation. Blindly importing the old function would discard
later composition, proxy, reentrancy, and ownership fixes. Port only the
Akane exception. The new TLOU2 branch inherits **all** Miles source changes
through `2c213e50`, including NRSTAB UI/ShadowFloor, NR safety and later
v0.8.8/0.8.9/0.8.91 NR additions, non-Reno FG hardening, and downloaded
Streamline plugin loading independent of MFG unlock. Do not cherry-pick the
old TLOU2 multi-mip/exposure commits: their base is v0.8.4 and their
successors are already in this v0.8.5-lineage source.

## Pass/fail requirements

1. Production policy recognizes only a non-null descriptor exactly 720x1000
   in `tlou-ii.exe`, case-insensitively. Null, different size, other games,
   and `tlou-ii-l.exe` (not observed with Akane here) must remain on the
   existing path. Do not broaden to all composition surfaces or all TLOU2
   surfaces without new runtime evidence.
2. For that exact helper surface, call the saved DXGI trampoline exactly
   once with the caller's original descriptor and return its result and
   swapchain unchanged. Never construct an OptiScaler wrapper or mutate
   current game-swapchain state for it. Preserve the current generic
   composition handling, including null/error and tiny-surface cases.
   A success log must only occur after a successful trampoline result and
   non-null returned swapchain.
3. Before code changes, a focused policy test must fail for the missing
   Akane rule on the Miles source. After porting, it must check positive,
   case-insensitive, wrong game, wrong size and null cases. Run the existing
   WARP composition smoke and full NR/MFG prerelease suite; compile the full
   Release/x64 RTX40-MFG target. Static checks cannot establish real Akane
   Present behaviour or absence of TLOU2 water/FG artifacts.
4. Verify the source diff from `1e397624`: the only production changes are
   the narrow Akane predicate and its use in composition creation. Record
   exact commit and DLL SHA-256. Package as a replacement `OptiScaler.asi`,
   not as Miles's `dxgi.dll` or a complete game installer.
5. If installing for a TLOU2 game test, first confirm the game is closed and
   back up the exact existing `OptiScaler.asi`, INI, OptiScaler and Akane
   logs with hashes. Change only `OptiScaler.asi`; leave Akane, NVIDIA,
   Streamline, loader and game-owned files unchanged. Verify installed hash.
   On crash, absent Akane UI, Akane Present failure, or new visual regression,
   preserve the test logs, restore the saved ASI and verify its hash.
6. The live acceptance is separate: Akane overlay loads without a Present
   error, OptiScaler log shows the exact helper pass-through and does not
   report it wrapped, NR/NRSTAB work as configured, FG off/on does not add
   immediate smearing, and a matched TLOU2 water scene is no worse than its
   old build. A Miles pass cannot substitute for this TLOU2 test.

No promotion to a generic main or release is implied by a successful build.

## Build and installation result (same day)

The exact Akane predicate and real DXGI/WARP composition smokes passed. The
full `tests/run_nr_prerelease.ps1` suite passed. Release/x64 RTX40-MFG built
successfully with inherited warnings and no errors; the build log is in
`04_BUILDS/BUILD-LOGS/TLOU2-V085-AKANE-20260924/build.log`.
Production commit `d0e2c6cb` embeds its own ID in the DLL. The copied ASI
package and installed `OptiScaler.asi` both have SHA-256
`D8517B5002054EDFBEFF2B157EE269193CD9B822020F3A438781DCFFB1BA6F2B`.
The original TLOU2 ASI, INI and three Akane/OptiScaler logs are preserved
under `05_BACKUPS/TLOU2-BEFORE-V085-AKANE-20260924`; the old ASI SHA-256 is
`81CBAC92EF760BD589286D3E47936A9AAFE8A249C23FCC8D4F31B46A0713A974`.
The game INI remains SHA-256
`80CBB4C71F0DD423D20F0724EAABF8728F8FC34F28389829BB47E48EC6431DD8`.
The package manifest is at
`04_BUILDS/CANDIDATES/TLOU2-V085-AKANE-20260924/MANIFEST.md`.

Live TLOU2/Akane acceptance is still pending. No game launch, gameplay
control, or changes to Akane, NVIDIA, Streamline, Miles, RDR2, or protected
main occurred during this candidate build and installation.
