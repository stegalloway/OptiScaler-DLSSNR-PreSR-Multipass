# v0.8.91 Preview / FG follow-up — isolated candidate

Date: 24 September 2026. Base: `e020f8dc` in
`candidate/v085-preview-fg-followup-20260923`. The installed, visually
accepted Miles DLL at the start of this work was SHA-256
`E293011B287A5B0B7C13514FD05D728A7D85FCEF11686CEEAD1F794BA1A3A342`.
Neither that checkpoint nor the separate RDR2/PureDark installation is changed
by source work in this candidate.

## Changes and explicit non-changes

- Port only the v0.8.91 Preview control from pinned upstream
  `f45ccf3a761df91450959d325ac0166cba5364c9`: menu checkbox and debug
  option, plus DX12/Vulkan routing of the immutable model input to the
  existing proxy view. This is an inspection view, **not** an image-quality
  enhancement. Preview uses debug view 4; NRSTAB is deliberately ineligible
  while a debug view is active. It must resume when Preview is turned off.
- Retain the already-integrated XeFG game-picture route from v0.8.9 and the
  v0.8.7 finished-picture cross-queue readiness check. No second copy or
  reinterpretation of either change is added. The production log reports
  `same producer queue` on first success and every 300 successful finished
  frames; `false` is evidence of a live *completed* cross-queue composition.
  The WARP queue fixture tests the wait/skip rule, but cannot prove a game
  takes that route. Silence or a same-queue line is not a cross-queue pass.
- Do **not** restore the old exposure scanner or cache. v0.8.5 removed the
  scan keys in `Config.cpp` and replaced CPU candidate scanning with same-frame
  GPU exposure meter/reduction in `DlssNr_Dx12_Encode.cpp` and
  `DlssNrFeature_Vk.cpp`. The current eight editable anchors are cheap direct
  settings, not a repeated 30-second discovery. No current code path needs
  scanner caching after a quality-mode change.
- Review but do **not** automatically port ShyVortex PR #7's blanket
  finished-picture cancellation for external FG or DLSS-G output. This branch
  has separate ordinary swapchain, Streamline game-picture handoff and
  OptiScaler-owned XeFG paths. A global cancellation condition could discard
  the valid game-frame picture in those paths. Keep the existing cancellation
  on NR/finished-picture disablement; revisit only with an ownership-specific
  reproducer and tests covering native Streamline, owned XeFG, and external
  PureDark handoff. This is an audited exclusion, not a claim that PR #7 is
  wrong for its own ownership model.
- The matched Miles Quality-versus-Performance FPS run is a measurement, not
  another feature in this build. It must use the same scene, display settings,
  NR state and FG state, and compare real-frame time and render extent, not
  generated-frame output alone.

## Predeclared live acceptance and rollback

1. Record DLL and INI hashes, save both in a new dated backup, and confirm
   Miles is closed before installation. Never install this generic DLL over
   the RDR2/PureDark ASI.
2. Boot Miles with ordinary accepted settings first. Confirm the candidate
   build identity in the log, NRSTAB's normal UI, and no startup smearing.
   Enable then disable Preview and confirm NRSTAB can arm again. A clean
   no-debug game is the control for any later FG experiment.
3. Test OptiScaler-owned XeFG only if runtime ownership is explicitly shown
   in the log; loading `libxess_fg.dll` alone is insufficient. If it cannot
   acquire the app-facing swapchain, mark live XeFG untested rather than
   crediting native DLSS-G.
4. With finished-picture mode explicitly enabled, require a production log
   line showing `same producer queue false` before claiming live cross-queue
   composition. A separate-queue input that is still pending must be skipped
   according to `FinishedInputReady`; production logs currently do not count
   those skips, so absence of an artifact cannot establish that branch.
5. For the FPS check, hold FG off and NR fixed for matched Quality and
   Performance windows, then record render extents, CPU/GPU frame times and
   Steam FPS. Repeat with NR on only as a separate pair. If scene or settings
   differ, label the result inconclusive.
6. Any crash, smearing, failed ownership, or missing NRSTAB UI: close Miles,
   retain logs and candidate, restore the exact backed-up DLL and INI, verify
   their hashes, and diagnose without touching known-good main.

## Automated gate

`tests/run_nr_prerelease.ps1` passed, including production-backed finished
queue, Vulkan, exposure and spatial tests. RTX40-MFG Release build and package
completed. These checks do not establish live FG visual quality or a
cross-queue route in Miles.

## Miles live result and FPS comparison (24 September)

- The installed candidate is `fa17156d` (`dxgi.dll` SHA-256
  `DA7B59B4BFFF42F14B6F15CE0A5D88E94F03452A66860951A04C7491236E988B`).
  The user reports no smearing and confirms Preview works. The log confirms
  NRSTAB returns to `ACTIVE` after leaving Preview. This does not establish
  XeFG ownership or a live cross-queue finished-picture pass; both remain
  unclaimed.
- A Quality-versus-Performance run with FG off showed the render extent
  changing from 2248x960 to 1688x720, while Steam base FPS stayed about
  74-75 with NR either on or off. The matching diagnostic windows were near
  74 presents/s. This does not mean DLSS mode failed to change the render
  resolution. It also does not isolate the source of the FG-off plateau.
- The subsequent FG-on run started with `numFramesToGenerate=2` (three total
  displayed frames per game frame). At Quality, NR off produced about
  202-222 present calls/s, corresponding to roughly 67-74 base frames/s;
  NR on produced about 99-140 present calls/s in settled windows, or roughly
  33-47 base frames/s. NR's measured GPU window was about 5.9-6.8 ms.
  The near-75 NR-off ceiling is consistent with output-rate/pacing pressure
  on the 240 Hz setup, so that value must not be interpreted as a free-running
  NR-off baseline. The NR-on result still shows substantial load; the exact
  causal FPS cost is not isolated by these scene-varying windows.
- The user reduced FG to one generated frame (`numFramesToGenerate=1`, two
  total frames) to remove the output ceiling confound. The log confirms the
  switch at 01:35:34. The only brief foreground diagnostic window after the
  switch is NR off, about 179 present calls/s (~90 base frames/s), with a
  transition/stall; the user observed about 100 base FPS in Steam with NR off.
  The subsequent windows are backgrounded at ~9 presents/s. The lower
  multiplier therefore lifted the observed NR-off result above the earlier
  ~75 FPS plateau, but the log does not establish the precise settled value.
  That first snapshot did not contain an NR-on comparison. After resuming the
  game, the user reported approximately 100 base FPS with NR off and 50 with
  NR on at 2x, and approximately 42 with NR on at 5x. The later log records
  about 217 presents/s in a 2x NR-off window, 101-122 presents/s in 2x
  NR-on windows, and 190-208 presents/s with NR on after Streamline reports
  `numFramesToGenerate=4` (5x). Dividing by the configured total frame
  factor gives approximately 109, 50-61 and 38-42 base FPS, respectively;
  these agree directionally with Steam's readings. Native Streamline's
  transition messages are not a per-frame FG-state trace, and the scene and
  periods away from the game are not perfectly controlled, so treat these as
  observed ranges, not a formal benchmark. The 2x pair demonstrates that
  reducing the multiplier does **not** eliminate the NR-on slowdown; the
  ~75 NR-off plateau at 3x was only part of the issue. It does not establish
  a Quality-versus-Performance scaling difference at the lower multiplier.
- Miles reports VSync off, but Streamline's RSYNC log reports `DRS VSync mode
  is Force ON` during native FG. This supports output pacing as a plausible
  contributor to the high-multiplier ceiling; it does not by itself prove
  the effective cap or identify which component requested it.
- Evidence is saved without replacing the live game files in
  `05_BACKUPS/MILES-PREVIEW-FG-FOLLOWUP-20260924`: the earlier
  `OptiScaler-fa17156d-fps-comparison-full.log` and
  `Miles-game-fps-comparison-full.log`, plus
  `OptiScaler-fa17156d-fg-multiplier1-20260924-snapshot.log` (SHA-256
  `95B06743E2932AC4A0A349F2A0187D8B9F3316B4C335293B44ED7E07CB42FA62`)
  and `Miles-game-fg-multiplier1-20260924-snapshot.log` (SHA-256
  `69075194BFC60BBC539EEC9468159D2F267CC631F307F4A934589CB2DEB3100C`).
  Two further active-game snapshots retain the later 2x NR-on and 5x windows:
  `OptiScaler-fa17156d-fg-multiplier1-20260924-after-resume.log` (SHA-256
  `DFD5D7F01D2AC91E114DDFF60C11DF8350CED4E29076997663294BF8A639763F`)
  and `OptiScaler-fa17156d-fg-multiplier5-20260924-snapshot.log` (SHA-256
  `6FD1BB3E9E8EBCBB9E5A304B16C3B63903AA7017B56A8D492C4D9CE034CD8A4D`),
  each with a correspondingly named Miles game log. All new copies are
  snapshots from an active game, not finalized session logs. The 5x INI is
  preserved as `OptiScaler-after-fg-multiplier5-test.ini` (SHA-256
  `4E6A660551CD74F04DCC782E245D8BF990A256C951493DE60AFAFDAFE2F75AAF`).
  `MILES_FPS_DIAG`
  counts swapchain Presents and its `fg_active=0` marker
  does **not** mean native Streamline DLSS-G was off; the Streamline lines are
  authoritative for that path.

## 4x throughput follow-up: settings-only candidate

The user clarified that NR was on for both the 2x and 4x comparison, and that
4x was chosen to approach the 240 Hz display's output rate. The 4x NR-on log
window at 01:40:51 shows about 189 swapchain Presents/s, below 240; therefore
the display-rate ceiling does not explain that window. It implies about 47
base frames/s if all four requested frames were presented, versus roughly 50
base frames/s at 2x NR-on in the same session. The 5x NR-on windows later show
about 188-208 Presents/s, or roughly 38-42 base frames/s. DLSS-NR GPU time
remained near 6 ms across those multiplier changes. The extra FG work and/or
its pacing is the plausible incremental cost, but the existing log does not
separate FG GPU execution from actual display-change timing. NVIDIA's
[DLSS-G programming guide](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_G.md)
warns that Present timing is not display timing and interpolated frames may be
dropped. No claim of exact displayed FPS follows from dividing Presents by
the requested multiplier.

At 4x, matching 50 base FPS would yield about 200 output FPS, not 240; 240
requires 60 base FPS. The low-risk experiment is to recover the small
4x-versus-2x base-FPS difference first, without touching FG kernel or pacing:
reduce the NR *model resolution* (not model strength) from 100% to 90% while
keeping 4x, NRSTAB, the current compression layout and other settings fixed.
This reduces nominal model pixels from 3024x1160 to about 2722x1044 (19%);
it does not guarantee proportional GPU-time or FPS savings and could soften
details. The model resources and NRSTAB history rebuild; wait for PATH READY
before any visual comparison.

Miles was closed before editing. The previous INI is preserved as
`05_BACKUPS/MILES-PREVIEW-FG-FOLLOWUP-20260924/OptiScaler-pre-4x90-20260924.ini`
(SHA-256 `4E6A660551CD74F04DCC782E245D8BF990A256C951493DE60AFAFDAFE2F75AAF`).
Only `OverrideInterpolationCount` changed from 4 to 3 (5x to 4x) and
`WorkingScale` from auto/100% to 0.900000; a two-line diff verified this.
The installed DLL remained SHA-256
`DA7B59B4BFFF42F14B6F15CE0A5D88E94F03452A66860951A04C7491236E988B`.
The 4x/90% test is **prepared, not yet gameplay-validated**. Pass requires
meaningful base-FPS or NR-GPU-time improvement in the same Miles scene with
NRSTAB ACTIVE and no unacceptable detail loss or smearing. Otherwise restore
the saved INI exactly; no new build is required.
