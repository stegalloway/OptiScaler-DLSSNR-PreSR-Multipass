# Isolated v0.8.5 + NRSTAB + ShadowFloor

User-directed narrower candidate after combined da7f1cac failed visual acceptance (smearing); cause undetermined.

Base: upstream v0.8.5 0dfd181a4e5e3bd0cdf9b5a2e75530fc616ad0b6, layout prerequisite 022129fb31bfef11f5858d7ab29419edd6257eca. Not based on da7f1cac. Known-good d3fcc16f and main f5d52994 remain untouched.

## Included

- d3fcc16f NRSTAB selector, startup motion-convention test, session convention cache, live A/B, K and output-pixel motion gate, geometry and placement resets.
- Necessary port dependencies: approved 512-byte constant layout; current-frame successful encode admission; exact-recording submitted/sealed completion before diagnostic readback; safe history/resource retirement.
- ShadowFloor lower-gain protection. Zero default preserves upstream darkening behavior. Ordinary resolve uses ResidualBlend only in that shader/pass; finished/deferred composition uses MvScaleX only in the finished shader/pass. Neither alias is used as a motion scale in those passes. Residual NRSTAB uses separate constants and real motion scales.
- NRSTAB controls in the expanded DLSS Neural Rendering panel, outside finished-picture and selected subpanel branches. Truthful inactive explanations, no forced route/setting changes.

## Excluded / unchanged

FG, Streamline, NGX parameter hooks, Reflex, provider policy, UI extent routing, downloaded-plugin load flag, all other FG files remain byte-for-byte upstream. No Reno, Warp Blend, magnitude work, local FG hardening, independent exposure-valid/held-meter change, additional mip-copy helpers or RDR2 coexistence overlay.

The upstream v0.8.5 exposure and multi-mip features remain as upstream supplied them. Ordinary EncodeInput uses the stock barrier and CopyResource paths; only successful-encode admission and ShadowFloor resolve constant are added. Run keeps stock active-color handling and copies the NRSTAB-selected source with the stock single-mip copy policy.

## Test contract

Frozen pre-edit scope is filed under workspace 07_DOCUMENTATION/TECHNICAL-DECISIONS/V085-nrstab-shadowfloor-only-20260920.md.
Existing shader behavioral assertions remain. Layout sentinel readbacks cover the appended field at byte 256. Embedded selector tests, exact-recording lifetime tests, production-extracted encode and host-copy bodies, and the full production RenderMenu call chain are exercised.

Menu test replays the old combined menu and must reproduce unreachable controls with ordinary NR; the candidate must pass pre/post x four subpanels, inactive/unsupported status, edits and resets. Host/widget seams do not establish in-game rendering or actual NR/FG runtime image quality.

No game installation or promotion. This is a generic DX12/bridge NRSTAB candidate, NOT an RDR2/PureDark coexistence build. Native Vulkan NR remains upstream; NRSTAB is unavailable on native Vulkan.

## Gameplay still required

Use the same scene, FG mode/multiplier, NR placement/model/settings and camera movement as the accepted untouched v0.8.5 control. Preserve existing INI; do not silently reset the user's 1.75-pixel gate. NRSTAB defaults if absent are enabled, K=1, gate=2px; ShadowFloor=0.
Verify controls visible, log integration=V085_NRSTAB_SHADOWFLOOR_DX12, startup WAIT_MOTION/READBACK to PATH READY/ACTIVE, then NRSTAB off/on while holding other settings fixed. Test ShadowFloor separately from zero. Quality-mode Q/P/Q must reset history and retain only compatible MV convention. Stop on smearing, instability, device removal or excessive VRAM growth and restore the separately backed-up game DLL.

