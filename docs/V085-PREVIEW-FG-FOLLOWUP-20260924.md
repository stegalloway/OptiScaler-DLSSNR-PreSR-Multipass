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
