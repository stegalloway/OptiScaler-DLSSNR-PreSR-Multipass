# Issue #97 A-C audit — 2026-09-27

Reference: issue #97 and the final merged ShyVortex PR #9 (b202d8c2).

This audit was performed after the DLSS-G pacing investigation and after PR #109 was integrated. The unsafe early
PR #9 revisions were not used as implementation references.

## A. DLSS-G OTA module lifetime / multi-provider patching

The current branch already implements a stronger lifetime model than final PR #9:

- LibraryLoad_Hooks.cpp classifies both nvngx_dlssg.dll and NVIDIA NGX OTA
  models\dlssg\...\*.bin paths before the generic .bin branch.
- Every successfully modified provider obtains and keeps a real loader reference for process lifetime.
- Prior patched providers remain retained when a later OTA provider becomes current. Their modified code therefore
  cannot be unloaded and have its HMODULE/base address recycled underneath stale patch state.
- Every new provider is independently checked for DLSS-G-specific exports, PE/image layout, supported gate
  signatures, and a complete transactional temporal/quality patch.
- A second provider that is genuinely DLSS-G but cannot be retained, validated or patched makes the higher-MFG
  path fail closed. Unrelated DriverStore/DirectSR images do not revoke a previously verified provider.
- Provider capacity is explicitly bounded at eight retained providers rather than silently overwriting identity.

The existing tests/mfg_unlock suite covers second providers, unsupported/failing handovers, retained-reference
lifetime, duplicate provider notifications, unrelated modules and provider-capacity exhaustion. It passed before
this audit. Because patched providers cannot unload, an additional pointer-only generation table would not improve
the address-reuse guarantee and was not added.
## B. Finished-picture NR vs presentation ownership

The branch already had a centralized DLSS-G ownership policy rather than PR #9's broad configuration predicate:

- successful Streamline SetOptions state seeds ownership;
- Streamline feature loaded/unloaded state becomes authoritative once observed;
- direct-NGX paths fall back to an exact fresh successful evaluation epoch, so a cached interpolation count cannot
  suppress NR after FG is off;
- cancellation remains a lifecycle decision and is intentionally separate from presentation ownership.

This audit found one remaining edge case: a direct NVNGX replacement can coexist with a separately observed
Streamline feature state. A fresh direct evaluation must still own the picture even if the unrelated Streamline
state says unloaded. RuntimeDlssgPresentationOwnership now composes those signals, but provider selection alone
does not count as activity.

The D3D11 wrapped finished-picture path now follows the same runtime-owner rule. It no longer bypasses ownership
just because the D3D12 queue-based AllowWrappedPicture predicate is inapplicable.

The native Streamline pre-FG handoff in DlssNr_StreamlinePicture is deliberately retained: that hook sees the
game picture before DLSS-G takes presentation ownership, so it is the correct place for finished-picture NR when
native DLSS-G is active.

## C. Swapchain mutex / fence / five-second wait audit

The final PR #9 local-mutex correction is already present in this branch's behavior: SetFullscreenState,
ResizeBuffers and ResizeBuffers1 acquire the local mutex unless Present already owns owner 4/5. There is no
configuration-based 'DLSS-G mod' bypass to port.

The audit found three distinct facts:

1. wrapped_swapchain.cpp contained a nominal five-second GPU-idle wait whose private resize fence/event started
   null and were only created inside a branch that already required them to be non-null. That wait was unreachable
   and performed no synchronization. It has been removed rather than retained as misleading dead code.
2. The real native-D3D12 FG queue-idle waits live in FG_Hooks.cpp. They can wait up to five seconds during
   FG swapchain replacement, ResizeBuffers/ResizeBuffers1, or FG swapchain release.
3. DlssNr_Dx12_FinishedQueue.cpp can independently wait up to five seconds for a submitted late finished-picture
   fence before swapchain resize.

Both real paths now emit FGWAIT diagnostics when [DLSSG] Diagnostics=true, including wait reason, fence values,
completion values and elapsed time. Failure/timeout remains visible even with diagnostics off. No timeout was
shortened: changing synchronization semantics before a runtime reproduction would trade a diagnosable stall for
possible resource reuse/device removal.

The DX11-with-DX12 bridge has separate five-second waits, but those are not on Miles Morales' native D3D12 path
and are outside the immediate Miles lifecycle test.

## Runtime validation required

The source-level A-C audit is complete. C's actual runtime attribution requires the next scheduled repeated
FG on/off lifecycle run with [DLSSG] Diagnostics=true. That run should determine whether any observed long pause
is NR finished-picture drainage or an FG queue-idle wait and which swapchain operation triggered it.
