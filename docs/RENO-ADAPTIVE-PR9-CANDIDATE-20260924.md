# Remaining-work umbrella candidate — pre-edit acceptance contract

24 September 2026. Isolated branch `candidate/reno-adaptive-pr9-20260924` starts at
`fed561fb`. This document precedes production edits. The accepted Miles and
TLOU2 installations, existing main and RDR2/PureDark build are controls, not
targets of this preparation.

## Pinned references and scope

- RenoDX 1.0: `9e510b0be45f88415efc1bd196f95c801f5ebc37`; the old local
  `889f7b2c` adapter supplies framework, Balanced, Aggressive and validated
  warp as restart-only experimental modes. It is a reference, not a patch to
  cherry-pick: its independent write/protection routine predates the current
  single MFG transaction and cannot be accepted in place of that transaction.
- RenoDX 1.1.5: `4406e4fadf4423afb500d8d9a08d5ace9a148d19` in
  `08_UPSTREAM-REFERENCES/MFGADA-RENODX-1.1.5`. Its Adaptive Quality Suite
  coordinates geometry, boundary, warp and inpaint stages. The upstream UI
  deliberately disables overlapping legacy experiments while Adaptive Quality
  is active. Its 1.1.5 workload optimization is part of this *new* kernel
  profile, not a demonstrated OptiScaler frame-rate improvement.
- ShyVortex PR #9: https://github.com/ShyVortex/OptiScaler-DLSSNR-PreSR-Multipass/pull/9.
  Our current loader already recognizes provider `.bin` paths and our
  swapchain already uses same-thread recursive-lock ownership. The remaining
  second-provider handover and active-external-FG finished-picture policy need
  separate, ownership-specific review. The PR's release-old-reference step
  is not safe to copy while old patched bytes may still execute.

Already present in the accepted candidate: optional RTX40 MFG unlock, Blackwell
retarget or PTX temporal correction, Streamline frame ceiling and plugin
selection, flip-metering option, accepted caller state, and atomic rollback-
capable gate/kernel/descriptor transactions. Generic Dynamic MFG controls exist,
but that does not establish equivalence to RenoDX's 1.1.5 controller. The old
Reno quality modes, Warp Blend, Adaptive Quality Suite, Automatic Latency Guard
and parked magnitude-only variant are not present in this branch.

## Required path separation

1. Keep the accepted current engine as the default and a byte-identical
   gameplay control. Quality additions are opt-in and restart-scoped.
2. First candidate: a *single* exact-provider Adaptive Quality profile from
   1.1.5, with legacy Reno 1.0 modes and Warp Blend disabled while it runs.
   No second MFG unlocker or ReShade addon is installed alongside OptiScaler.
3. Legacy framework/Balanced/Aggressive and standalone warp may be separate
   A/B profiles, never stacked over Adaptive Quality. A warp-on run is its own
   comparison because earlier RDR2 evidence suggested an adverse NRSTAB
   interaction; presence in a binary is not proof that the warp mode ran.
4. Magnitude-only remains a distinct profile. Its offline 38,432-byte cubin
   fit and CPU checks are not GPU/load/game proof. The 32 internal-grid-unit
   placeholder must be calibrated to game display pixels before gameplay.
   Balanced-plus-magnitude remains 128 bytes over its slot and must not be
   truncated or substituted for this profile.
5. Reno Dynamic MFG, HDR/UI input compatibility and Automatic Latency Guard
   each require a separate ownership/ABI audit. The latency controller may
   lower the multiplier or trim source rate; it is not an unconditional fix
   for the 2x-to-4x base-FPS cost. Begin with read-only timing, not automatic
   control. Preserve current game-owned Streamline options and Reflex handling.
6. PR #9's external-FG picture gate must use fresh active-runtime evidence and
   restore the NR path when FG turns off. A cached count or unlock checkbox is
   not sufficient. A second provider cannot make `Pending()` permanently true,
   release a patched module reference, or report success before all modules it
   relies on are retained and safely patched.

## Failure tests frozen before implementation

- Exact 310.9.1 provider, export identity, all kernel payload hashes, unique
  roles/slots and option compatibility must pass before any write. Unknown,
  mixed, duplicate, already modified or partial layouts make zero writes.
- For every gate, kernel and descriptor write: inject failure before and after
  mutation. Complete rollback reports failure and keeps FG to its native
  accepted count; incomplete rollback retains affected allocations/modules and
  refuses evaluation. Protection restoration and cache-flush failures count.
- Attempt base-DLL then OTA `.bin` admission, duplicate admissions, unsupported
  second provider and failure after a successful first provider. An old patched
  provider stays retained for process lifetime unless its original bytes are
  *verified restored* and no execution can use it. No false success on capacity
  exhaustion or failed retention.
- With FG on/off/on, finished-picture NR must be suppressed only when the
  external FG integration actually owns an active interpolation path. With FG
  off it resumes; OptiScaler-owned XeFG and native Streamline picture paths
  retain their existing ordering. Test same-thread recursion and cross-thread
  lock exclusion separately.
- Every quality profile must report its requested and *actually applied*
  components. Missing generated tables or a failed kernel selection must not
  silently fall through to another quality mode. Legacy modes cannot overlap
  Adaptive Quality in source, patch plan or UI state.
- Run existing MFG, FG caller/ABI, NR prerelease, swapchain and build checks
  unchanged. Validate actual CUDA kernel loading/selection on the RTX 4090
  before a game install. A build or CPU fixture pass alone is not visual proof.

## Live acceptance, after offline gates

Back up exact installed DLL/ASI, INI and logs; verify provider and control hashes.
Use Miles first: same scene, NRSTAB K=1 and 2px, fixed multiplier, FG off/on,
and separately no-NR and no-FG controls. Do not change several quality policies
in one A/B run. Require no immediate smearing, clean boot/exit and log proof
that the requested kernel profile actually loaded. Then run TLOU2 with Akane,
including settings changes and its water scene. Only after both controls pass
should RDR2/PureDark be considered; its ownership and epoch rules remain
separate. Any regression restores the verified known-good DLL and preserves the
failing log and exact settings.

No source code or game installation is changed by this contract.

## Full remaining-work register (user amendment, 24 September)

The request is to *try to bring all remaining relevant work into this new
candidate before RDR2*, including the parked magnitude experiment and PR #9.
"All" means every item gets an explicit disposition and an individually
testable candidate path; it does not mean enabling mutually contradictory
kernel policies simultaneously, nor importing a ReShade addon wholesale.

| Item | Current disposition in `fed561fb` | Umbrella-candidate work |
| --- | --- | --- |
| PR #6 transaction/FG caller hardening and wilsjo2 NR PR #1 | Selectively integrated and accepted in Miles/TLOU2 | Preserve unchanged; rerun failure and gameplay controls. |
| v0.8.5–0.8.91 NR reconstruction, exposure, multi-mip, Preview, XeFG queue ordering and downloaded Streamline selection | Present in source; Miles/TLOU2 acceptance is narrower than every optional path | Preserve; no duplicate cherry-pick. XeFG needs a separate live owner-specific target, not a Miles DLSS-G claim. |
| Reno 1.0 framework/Balanced/Aggressive and standalone warp | Absent from this candidate; old working-main port archived | Optional mutually exclusive legacy A/B profiles after safe transaction adaptation. Warp remains off by default. |
| Reno 1.1.5 Adaptive Quality and workload optimization | Absent | Highest-value new quality profile to port and verify, exact provider and kernel-load gates; no stacking with legacy modes. |
| Reno Dynamic MFG, HDR/UI input guard and latency work | Current host has some native Dynamic MFG/Reflex/HDR controls, not proven equivalent | Audit each exact ownership/ABI overlap. Integrate only nonduplicate compatible pieces, with diagnostics/read-only mode before automatic control. |
| Parked magnitude-only experiment | Compile-only fit and CPU proof; no runtime path | Calibrate grid units, integrate as a separate optional profile with matched control, verify kernel selection and gameplay; not a silent Adaptive/Balanced option. |
| ShyVortex PR #9 | OTA recognition and same-thread locking covered; safe handover and active-runtime picture policy unproven | Implement only after lifetime and on/off failure tests pass; do not release live patched providers. |
| ShyVortex PR #7 external-FG cancellation | Not equivalent to current ownership policy | Review against real external-FG ownership separately; no automatic PureDark/RDR2 carryover. |
| Exposure scanner caching | Obsolete scanner not present in v0.8.5 host | Closed as inapplicable; do not recreate it. |
| Quality-versus-Performance and 2x-versus-4x cost | Saved Miles evidence; scene/cap/multiplier confounds | Run matched timing/quality controls on accepted candidate and again per added profile; no detail reduction as an assumed solution. |

`v0.8.91 Preview` is already in this source lineage; the earlier note that it
was excluded described an older candidate. RDR2/PureDark and ShyVortex rebase
remain outside this generic umbrella's *deployment* target until Miles and
TLOU2 are stable. No game or runtime binaries are replaced merely to complete
the register.

## First offline result after the specification

The installed Miles provider is SHA-256
`FF6E90EB78B827927DFF5B4ECC6B1C870C2E9BCA29ED9F48C7D348CC9E170B82`;
the pinned ptxas is
`BA758767DA154A6BE6FE798CD906DBC4B69E79FEC782461EF7`. The unmodified
Reno 1.1.5 generator produced seven source-profile-matched variants in
`06_TEST-RESULTS/RENO-ADAPTIVE-20260924` without touching a mapped provider.
`adaptive_quality_geometry_v1` is 39,712 bytes in a 39,968-byte scatter slot;
`adaptive_inpaint_decision_v1` is 12,704 in a 15,136-byte slot. The blend
kernel's original *recompiled baseline* is 17,056 bytes, already larger than
its 16,800-byte native slot, so the generator correctly skipped in-slot blend
variants. A separately allocated, identity-verified descriptor redirection
would be required for that component. This is a hard integration gate, not
permission to truncate or silently call the partial suite complete. The
upstream silhouette and quality-refinement CPU tests passed, as did its PTX
fragment JIT check (18 registers in the synthetic test); none is a game or
NVIDIA FG runtime pass.

The generated-header SHA-256 is
`00D1D15B1B6AAB42C5BDFF2D752A524EFBE9D2EE9593D6911EC8B1D72E5438A6`.
The current unmodified candidate also passed all 22 MFG patch cases, its
rollback/protection/cache suite, provider/ceiling/PTX/method/flip smoke checks,
and a read-only mapped-provider smoke (`1/1` gate family, 31 retarget groups).
The disk provider hash remained unchanged. These are the baseline assertions
that later quality and PR #9 edits must preserve, not proof of their success.

## Implementation checkpoint — quality branch, 24 September

The isolated candidate now has restart-scoped modes 1–3 and Adaptive mode 4.
Mode 0 is unchanged. Adaptive forces its validated warp stage and cannot be
stacked with the legacy warp toggle. Quality planning verifies the exact
provider, all three roles and original payload hashes; one shared transaction
owns gates, cubins and descriptor redirects. A failed plan writes nothing; a
clean transaction failure rolls back, and incomplete rollback retains the
provider/allocation and refuses the unlock. The old independent quality writer
was not copied. Generated provider-derived headers are ignored by Git; their
hashes and upstream license are in `mfgquality/UPSTREAM.md`.

Release x64 RTX40 compiled. The installable DLL is rebuilt from the committed
candidate and its final SHA-256 belongs in the package manifest.
The existing 22 MFG patch cases, transaction safety checks, 7 exact-provider
mode/warp mappings, and new quality failure cases (late clean rollback, unsafe
rollback retention, changed-source fingerprint) passed. The entire NR
prerelease suite passed. On the actual RTX 4090 the CUDA driver loaded all
baseline and seven variant cubins, and selected both legacy and Adaptive warp
PTX from the production adapter. No CUDA kernel or game FG was executed;
visual quality, frame pacing and crash-free gameplay are **unverified**.

PR #9's second-provider handover is now implemented without releasing the
first patched DLL: at most eight distinct providers may be retained, duplicate
admission is inert, and capacity exhaustion or a failed second-provider
retention/identity/patch refuses higher FG. A failed clean patch releases only
the new reference; incomplete rollback keeps every affected provider and
redirect allocation. The failure test first reproduced the missing handover,
then passed after the change. Synthetic second-provider success, unsupported,
late clean failure, unsafe rollback, failed reference retention and capacity
tests now pass. The loader no longer uses `Pending()` to exclude OTA providers
after the base DLL succeeds. This is production-source CPU evidence, not a
live OTA runtime test.

PR #9's proposed finished-picture suppression is not copied by unlock setting
or cached count: this branch already uses a separate pre-FG Streamline picture
handoff, and its existing owner-specific tests pass. PR #7 is an upstream
merge whose external-FG cancellation has different call-site ownership; it is
not a general-purpose cancellation to inject into XeFG/PureDark paths.

The magnitude-only research cubin (SHA-256
`75054B41D03DFA489E09DC62F760C01FE8BCE210497C18489363128802444762`)
now also passed CUDA **load and function selection** on the RTX 4090. It is
still an offline-only experiment: its 32-grid-unit threshold is uncalibrated
to display pixels, and no gameplay use is authorized by that probe. It is not
misrepresented as a completed fifth quality mode.

The existing `[DLSSG] AdaMfgQualityMode` and `AdaMfgWarpBlend` INI key names
are preserved from the earlier working-main Reno experiment. An old `auto`
value remains the mode-0 default; no existing Miles INI is changed merely by
building this candidate. On a second-provider failure the overlay now gives
the refusal precedence over the first provider's previously successful kernel
count, so it cannot show a green applied claim while higher FG is disabled.
