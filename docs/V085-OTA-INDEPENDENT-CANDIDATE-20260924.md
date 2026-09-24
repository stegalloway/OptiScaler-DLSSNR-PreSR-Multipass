# v0.8.5-lineage downloaded-Streamline-plugin candidate

Date: 24 September 2026. Branch:
`candidate/v085-ota-independent-20260924`. This candidate starts from the
visually accepted `fa17156d`-lineage Miles build and the isolated
`6bbf9a5b` plugin-matched diagnostic. The protected working Miles `dxgi.dll`
is SHA-256 `DA7B59B4BFFF42F14B6F15CE0A5D88E94F03452A66860951A04C7491236E988B`.

## Change and boundary

In the RTX40-MFG build, allow loading downloaded Streamline plugins when the
host already permits OTA updates, **regardless of `AdaMfgUnlock`**. Preserve
the host's separate OTA permission; do not enable loading in OptiScaler-owned
Streamline sessions that deliberately disabled OTA. Do not change the MFG
provider patch, FG count, NR, NRSTAB, exposure, shaders, or game INI.

`eAllowOTA` checks/downloads updates; `eLoadDownloadedPlugins` selects
downloaded plugins for this session. Miles's working build did the former
but withheld the latter when MFG unlock was off. Its log explicitly said
`eLoadDownloadedPlugins flag not passed to preferences, OTA'd plugins will
not be loaded!`, and selected local `sl.dlss_g` 2.9.0 although downloaded
2.14.0 was available. The independent-load diagnostic selected downloaded
2.14.0 with unlock both off and on. This candidate makes that selection a
standalone policy, not a side effect of the unlock.

## Predeclared checks and rollback

1. Build Release x64 RTX40-MFG and run the existing prerelease and MFG
   safety checks. Verify the candidate contains no production change beyond
   this flag policy. A build/test pass is not a visual-game pass.
2. Before installation, confirm Miles is closed, save the exact installed
   DLL and INI with SHA-256, and install only into Miles. Do not touch RDR2
   or the known-good source branch. Leave the user's currently selected
   native 2x / full-NR settings intact.
3. On the first native Miles launch, require log evidence that MFG unlock did
   **not** patch the provider, the downloaded `sl.dlss_g` plugin was chosen,
   and the game uses 2x FG. Run the same scene and FG off/on transition;
   check for immediate smearing, freeze, or missing NRSTAB UI. The older
   plugin and scene-varying FPS logs alone cannot serve as the A/B control.
4. If clean, compare the candidate's native-2x settled frame windows with
   the prior diagnostic downloaded-plugin native-2x run. Present-call rate
   is not guaranteed displayed FPS. An unlocked-2x comparison requires a
   separate restart and must not be conflated with a mode toggle.
5. On crash, smearing, failed plugin selection, or other regression: close
   Miles, preserve the final log, restore the exact backed-up DLL and INI,
   verify hashes, and leave the candidate unpromoted. No change reaches main
   without a Miles visual pass; other games remain untested.
