# RDR2 PureDark overlay on v0.8.5 working-main

## Base and branch

- Base branch: `working-main`
- Base HEAD: `42c34132f254f0d328fe5d5aee4cd7f93d550e32`
- Validated runtime/source beneath it: `144c2ba4035b33eac9eb9204b346b1f03ae31a92`
- RDR2 branch: `rdr2/puredark-on-v085`
- Historical source overlay: `6368c23fab1cf335c9ba67f715734b54993f52a8`

This remains a **separate RDR2/PureDark build**. Never put this RDR2 build into Miles/TLOU2/other games, and never put the generic main build into the PureDark RDR2 setup.

## Ownership boundary

For `RDR2.exe`, PureDark owns:

- DXGI and presentation
- Streamline
- Reflex
- frame generation and its Numpad `*` toggle
- PureDark's End menu

OptiScaler keeps:

- NGX upscaling and DLSS-NR
- device-level D3D12 hooks required by SR/NR
- the user-selected Ada MFG kernel/count patcher
- only the narrow DLSS-G `slDLSSGSetOptions` / `slDLSSGGetState` bridge needed for the MFG count override

Global DXGI, D3D11, D3D12 export/Agility hooks and generic Streamline interposer/DLSS/local-DLSSG/Reflex/PCL/common interception are bypassed for RDR2. Generic HDR/UI and presentation-ownership signals are not used to infer PureDark ownership.

The current main generation-safe DLSS-G module/hook lifetime code remains authoritative. Same-base plugin reloads use generation identity and INFO retirement logging; the RDR2 overlay does not restore any old single-HMODULE assumptions.

## Private NR epochs

RDR2/PureDark uses a private D3D12 NR recording/submission clock:

- an RDR2-only command-list `Reset()` hook is installed for process lifetime;
- the real Reset is called first;
- successful Reset advances recording generation and expires a dropped unsubmitted recording;
- failed Reset does not;
- NR evaluation registers the recording before the game submits it;
- the private submission epoch advances only after a matching list reaches the post-`ExecuteCommandLists` callback;
- one matching queue batch advances the epoch once, not once per list;
- pending recordings are capped at 256 and overflow never invents a submission;
- clock state clears only after finished-picture work has retired during shutdown.

Current main no longer contains the historical ExposureScan subsystem. The extracted clock helper retains its tested scan-generation behavior, but no obsolete `ExposureScan::Tick` hook is reintroduced.

A current-main refactor otherwise replaced native DX12's supplied submission epoch with global `frameCount`. The RDR2 overlay explicitly preserves the private epoch at that seam **only for RDR2/PureDark**. Other games keep main behavior unchanged.

The known limitation remains: COM destruction/address reuse without a successful Reset is not globally observed. Unusual multi-view/proxy/asynchronous paths still need live validation.

## Main features inherited unchanged

Do not re-port or fork these in the RDR2 commits:

- PR #109 `MatchGuides` and `RenderMotionScale`
- spatial-compression motion-scale reference fix
- NRSTAB guide-domain hardening
- configurable NRSTAB minimum dimension/aspect tolerance
- per-mode MV direction cache
- generation-safe MFG provider/plugin patching and retirement logs
- MFG quality/magnitude/latency work
- non-throwing NR compatibility runtime fix
- build/provenance tooling

Any later generic NR/NRSTAB quality change must land on `working-main` first and then this short RDR2 series must be rebased.

## Current PureDark runtime

The backed-up live RDR2 installation currently contains Streamline 2.7.2 and:

- `nvngx_dlssg.dll` version **310.9.1.0**
- SHA-256 `FF6E90EB78B827927DFF5B4ECC6B1C870C2E9BCA29ED9F48C7D348CC9E170B82`

The older 310.1.0 limitation in the historical document is no longer true for this installation. Do not change/downgrade/copy PureDark runtime files during this port.

First compatibility validation should still use MFG quality mode 0 / Warp off so the port is tested independently of quality-profile experiments.

## UI

The OptiScaler overlay is disabled only for RDR2. PureDark's End menu and Numpad `*` FG toggle remain native. No second RDR2 menu/hotkey implementation is added.

## First compatibility run

Use the previous known-good RDR2 INI and placement. Do not change count/provider/runtime during this run.

Required log/behavior evidence:

- PureDark FG works; Numpad `*` toggles it.
- PureDark End menu remains intact.
- RDR2 ownership/bypass lines are present.
- DLSS-G plugin generations are patched and hook retirement shows no `1E7` or blocked state.
- private NR submission epoch announces and advances.
- NRSTAB reaches `PATH READY` / `ACTIVE`.
- the DLSS-NR model motion-scale diagnostic is approximately **1.5** for the 2293x960 -> 3440x1440 RDR2 path.
- no device removal.
- normal exit.

## Smear A/B after compatibility passes

Use the same horse ride/route/time/weather, roughly 30 seconds per step:

1. `RenderMotionScale=true`
2. `RenderMotionScale=false`
3. Apply model off, NR still running
4. NR fully off

Interpretation:

- true clearly better than false: motion scaling was causal; keep main default on.
- true/false similar but apply-model-off clears it: NR output is causal; any motion fade belongs on main behind a switch.
- apply-model-off does not clear it: investigate timing/PureDark FG instead of changing NR.

## Backup and rollback

Pre-port backup:

`09_ARCHIVE/GAME-DEPLOYMENT-BACKUPS/RDR2-WORKING-BEFORE-V085-PORT-20260928`

Recorded original hashes:

- `OptiScaler.asi`: `1825A7F30350361F312D97B50FE476CDA147F6ADC8ADC083FD3DC30B716491CC`
- `OptiScaler.ini`: `58DA516B1FD4A2086FA59C2B63206EDFE41C12D25F986F022F0B5F093C7C0B77`

The complete `mods/UpscalerBasePlugin` tree is backed up. Deployment of the port must replace only `OptiScaler.asi`; preserve the live INI and all PureDark files.

If startup, UI, FG, NR, or stability regresses, exit and restore the backed-up ASI/INI. Do not use this modded setup in Red Dead Online/anti-cheat modes.
