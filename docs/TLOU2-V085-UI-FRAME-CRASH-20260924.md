# TLOU2 candidate settings-menu crash and isolated UI-frame fix

24 September 2026. The first `d0e2c6cb` TLOU2/Akane candidate ran until
settings were changed and the OptiScaler menu closed at 14:15:57. The game
then crashed. This is a confirmed OptiScaler access violation, not an Akane
Present failure or an NRSTAB image-quality conclusion.

## Preserved evidence

- Windows Application Error 1000: `tlou-ii.exe`, fault module
  `OptiScaler.asi`, `0xc0000005`, module offset `0x308079`.
- The game's minidump places the fault at a read of `[rcx+0x40]` with
  `rcx=0`. The adjacent instructions prepare colours `0xFF0073FF` and
  `0xFFFFD200`, exactly the orange work-region and cyan centre-outline
  colours in `DlssNr_MenuOverlay.cpp`. Release export names shown by WinDbg
  are not function names for this code; the build has no matching linker PDB.
- `OptiScaler.log`: INI save at 14:15:56.905; menu visibility 1→0 at
  14:15:57.305; log ends. `AkaneCore.log` reports the access violation at
  14:15:57.307 and saves the minidump. Earlier INI saves and menu closes
  did not crash.
- The post-crash INI has `SpatialCompression=true` and
  `SpatialShowCenter=true`; both were absent/neutral in the pretest INI.
  Other settings also changed, so the INI diff alone would not establish
  the cause. The crash instruction and outline colour constants do.
- Log, INI, Akane logs, minidump, and crashed ASI are preserved in
  `05_BACKUPS/TLOU2-V085-AKANE-CRASH-20260924-1415`. Minidump SHA-256:
  `EB93F7EEE318245FFCCFA2C733F9D3BC80129C2CD4F4D90FA8DF7BCF01253E76`.

## Cause and correction

`MenuCommon::RenderMenu` calls `RenderPerformanceOverlay` even when no ImGui
frame was started. `RenderNrCompareTags` calls `RenderSpatialOutlines`, which
uses `ImGui::GetForegroundDrawList`. Before the fix, the NewFrame condition
covered the menu, FPS display, notifications and comparison tags, but not
spatial outlines. With the menu closed and only the outline active,
`GetForegroundDrawList` accessed ImGui's null current window.

Commit `d7fe49b3` changes only the candidate: spatial outlines request a
frame while NR and spatial compression are enabled; comparison/outline
drawing is skipped unless a frame actually started. This keeps the outline
visible after the menu closes and prevents any call to that draw path
without a frame. It does not alter NR, NRSTAB, FG, Akane, or the game INI.

## Verification and limits

- New production-backed menu-frame predicate smoke passed.
- Full `tests/run_nr_prerelease.ps1` passed.
- `tests/run_akane_compat.ps1` passed, including real DXGI/WARP composition.
- Release/x64 RTX40-MFG DLL compiled and linked. The old upstream post-build
  packaging script failed because it does not quote a workspace path with
  spaces; rerunning with that packaging event disabled completed cleanly.
  The ASI was packaged manually and hash-verified.
- Installed fixed ASI SHA-256:
  `E4AA8F8C3471E9BDCAEC67411148D8C110F4BDBE83621A4AC3FAD740E2FA7635`.
  The crash-session INI was deliberately left unchanged, SHA-256
  `B58507B112B1EAD232A43AD7DFC5307D0B7351DFEBABE743BE72C0534D712359`.

Live validation remains open: launch TLOU2 with centre outline enabled,
close/reopen the OptiScaler menu, change one outline setting and close it,
then check Akane overlay, NRSTAB and FG off/on. No game launch was used to
claim this fix. If the game crashes again, preserve the new log and dump
before changing the installation. The pretest older ASI and INI remain in
`05_BACKUPS/TLOU2-BEFORE-V085-AKANE-20260924`.
