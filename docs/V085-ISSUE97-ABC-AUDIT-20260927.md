# Issue #97 A-C audit

Date: 2026-09-27

Reference: ShyVortex PR #9 final merge `b202d8c2f2fd88b2ec842073aa3efe59da2a9569`.

This audit uses the final merged revision, not the earlier unsafe versions that changed the mutex expression,
made MFG patching permanently pending, or suppressed finished-picture NR from configuration intent alone.

## A. DLSS-G provider / plugin lifetime and OTA handover

The current branch was already stronger than the merged PR for the NGX provider:

- the game/base `nvngx_dlssg.dll` and `models\dlssg\...\*.bin` paths are recognized before NGX consumes them;
- a successfully modified provider keeps a real loader reference for process lifetime;
- multiple distinct supported providers can be retained and patched;
- unrelated DriverStore or DirectSR-only modules do not revoke an existing successful unlock;
- provider identity, PE layout and both gate signatures are validated before any write;
- gate + temporal/quality changes are one transaction with rollback handling;
- second-provider retain/signature/transaction/capacity failure cases are already covered by the MFG regression suite.

Because modified providers are retained, a patched provider HMODULE/base cannot be unloaded and recycled underneath
the patch registry. This removes the main HMODULE-reuse hazard for provider code.

The gap found by this audit was the Streamline `sl.dlss_g` plugin layer. It previously kept raw HMODULEs in
`g_plugins` / `g_pluginsTried`, so a real unload followed by a reload at the same base could be mistaken for
the old module. Software flip-metering also stopped globally after the first plugin reported `patched`, so a
later plugin generation could miss that patch.

The branch now tracks live plugin generations independently. The existing Kernel32/ntdll unload hooks notify the
registry after the real unload call; a generation is retired only when Windows confirms that address is no longer
mapped. A later load at the same base is then inspected and patched as a new generation. Provider and plugin logs
include generation/base/path/image-size/timestamp where applicable. An already-neutralized ceiling is recognized
explicitly, so a harmless reference-count cycle does not turn into an unknown-signature warning.

## B. Finished-picture NR versus presentation ownership

No PR #9 config-driven suppression was copied. The current branch already has a centralized runtime policy in
`DlssNr_FinishedPicturePolicy.h`.

Ownership evidence is:

- successful Streamline DLSS-G options while feature state has not yet been observed;
- Streamline feature load/unload state once available;
- a fresh successful DLSS-G evaluation for the same present epoch as fallback/direct-NVNGX evidence;
- direct NVNGX provider selection only when paired with that fresh evaluation.

A stale interpolation count alone is not ownership. An explicit successful Off request releases ownership.
Miles' feature-unloaded signal overrides its known stale eOn options state. Finished-picture cancellation remains a
lifecycle decision and is intentionally separate from the presentation-owner predicate.

The policy regression test covers stale evaluation epochs, feature unload overriding stale options, direct-NVNGX
freshness, D3D11/D3D12 wrapped-picture routing, paused internal FG, and cancellation separation.

## C. Swapchain mutex / fence / 5-second waits

The current `_localMutex` rule already matches the safe final PR #9 shape without its obsolete `isDlssgMod`
bypass:

- `Present` owns local-mutex owner 4.
- `Present1` owns owner 5.
- `SetFullscreenState` takes owner 3 only when the current thread does not already own 4/5.
- `ResizeBuffers` takes owner 1 only when the current thread does not already own 4/5.
- `ResizeBuffers1` takes owner 2 only when the current thread does not already own 4/5.

`DlssNr::WaitForFinishedPicture()` is called before ResizeBuffers/ResizeBuffers1 acquire the local mutex. Its
finished-picture queue refuses to wait on producer recordings that are not idle, cancels late work first, and only
uses the 5-second fence wait for a submitted slot whose completion fence is still outstanding.

The other relevant 5-second wait is `FG_Hooks.cpp::WaitForQueueIdle`, used for OptiScaler-owned FG swapchain
resize/release/create transitions. It is not a reason to weaken the wrapped-swapchain mutex for native external
DLSS-G.

No archived Miles test log contains a queue-idle or finished-picture 5-second timeout/failure signature. The current
diagnostic build can still identify any recurrence: `[FGWAIT]` reports wait kind, caller/reason, fence values,
elapsed time and wait result under `[DLSSG] Diagnostics=true`.

Conclusion: do not transplant the old De Morgan expression or a blanket "DLSS-G configured => skip locking" rule.
The present-owner recursive bypass is the correct lock rule. Any future 5-second stall should be fixed at the
specific fence/ownership edge identified by `FGWAIT`, not by globally removing synchronization.

## Validation

Passed on 2026-09-27:

- `tests/mfg_unlock/run.ps1`: all provider, second-provider, transaction, provider-discovery, ceiling, PTX,
  temporal-method and flip-metering cases.
- `tests/run_nr_finished_picture_policy.cmd`: passed.
- `tests/owned_mutex/run.ps1`: same-thread recursive bypass and other-thread blocking passed.

## Miles lifecycle acceptance (2026-09-27)

Test build: `f1b0626f0c9aa8d86273afba1aa79a08acfbe9b0`
(`dxgi.dll` SHA-256 `8571A1C1EC89991697A580E2B1F6FC6F5745E6BFA197A4838CC5451585C2113A`).

Observed sequence included FG off, 2X, off, 6X, off and a lower multiplier again. The run completed normally.

Key evidence:

- zero `[FGWAIT]` lines and no queue-idle / finished-picture timeout;
- ownership switched to `false` on each observed Streamline DLSS-G feature unload and back to `true` on reload;
- the first local `sl.dlss_g.dll` really unloaded and then reloaded at the same base address; generation tracking correctly changed from plugin generation 1 to generation 2 rather than treating it as the old module;
- the NVIDIA OTA Streamline plugin loaded separately as plugin generation 3 and had its own frame-count clamp patched;
- provider generation 1 (`nvngx_dlssg.dll` 310.9.1) patched successfully;
- the 6X phase forwarded five generated frames and the subsequent feature unload released finished-picture ownership despite stale prior SetOptions state;
- no device-removed, crash or deadlock signature occurred.

The Streamline `Setting different 'common' constants multiple times within the same frame` messages are pre-existing noise, not introduced by this change: the archive already contains hundreds of occurrences, including the earlier cap-off controlled sweep.

Result: A-C lifecycle acceptance passed on Miles Morales. Keep `[DLSSG] Diagnostics=false` for normal use.

## DLSS-G stale-detour follow-up (2026-09-27)

The final Miles combined run exposed one benign startup error: `Failed to unhook DLSSG: 1E7`
(`ERROR_INVALID_ADDRESS`). The first `sl.dlss_g` generation had already been unmapped before the replacement
generation was hooked, but the old Detours trampoline pointer was still non-null, so `unhookDlssg()` attempted
to detach through dead module state.

Follow-up hardening:

- the active DLSS-G hook now records its owning HMODULE;
- the existing post-loader-unload notification retires the hook bookkeeping only after Windows confirms that image
  is no longer mapped, so ordinary reference-count decrements do not discard a live hook;
- `unhookDlssg()` also refuses to detach if its recorded owner is already gone;
- a genuine detach-transaction failure keeps the live hook bookkeeping so a later call can retry instead of losing it;
- the DLSS-G detach transaction contains only the `slGetPluginFunction` hook, so a failed detach cannot leave
  Reflex/common/interposer hooks partially batched with it.

This removes the misleading `1E7` path while preserving a real detach error if one occurs against a still-live
plugin.
