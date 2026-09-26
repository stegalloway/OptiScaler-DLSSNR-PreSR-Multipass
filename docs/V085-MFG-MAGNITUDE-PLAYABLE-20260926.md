# v0.8.5 calibrated magnitude-only MFG profile — 26 September 2026

## Scope

This branch adds a separately selectable **calibrated magnitude-only** Ada MFG quality
profile (`AdaMfgQualityMode=5`) for the exact DLSS-G 310.9.1 provider.

It is not Adaptive Quality, does not stack with the legacy framework profiles, and
does not silently enable Warp Blend. The profile replaces exactly the validated
motion-vector scatter kernel; native inpaint, inpaint-decision and warp behavior stay
unchanged.

The provider-derived cubin table is intentionally local and ignored by Git. A normal
source-only checkout therefore compiles without NVIDIA payload bytes and refuses mode 5
cleanly rather than substituting another profile.

## Live calibration

Miles Morales supplied the missing display mapping through the real
`NvAPI_D3D12_LaunchCuKernelChain` scatter route:

- final output: **3440x1440**
- motion grid: **1720x720**
- mapping: **2 display pixels per motion-grid unit on both axes**
- launch grid: **108x45x1**
- block: **324x1x1**
- native parameter floats: **207.36, 40**
- phase scale: **1**
- flag: **1**

This converts the validated threshold sweep to:

| Display threshold | Grid threshold | Squared grid threshold | Cubin SHA-256 |
| ---: | ---: | ---: | --- |
| 16 px | 8 | 64 | `E9176C7D4923520852A5AB52B70A520E9F4F8A6A89CA4307BBAEB01FEC74843C` |
| 32 px | 16 | 256 | `DACBA2C3732A31566F4FD516B9581C7828503D122BDD16417076EA444AB633FD` |
| 48 px | 24 | 576 | `A4748BA26D085DB86E0EBFE5FC326DDA24A2EC34BEFACE3C3FE2959C4E0D09EB` |
| 64 px | 32 | 1024 | `75054B41D03DFA489E09DC62F760C01FE8BCE210497C18489363128802444762` |

All four cubins are 38,432 bytes in the provider's 39,968-byte slot, use 39
registers, 7,776 bytes shared memory, and have zero stack/spill stores/spill loads.
The 64 px arm reproduces the earlier research cubin exactly. It remains the configuration
default only as the legacy/control arm; this is **not** a claim that 64 px is visually best.

## Runtime contract

Mode 5:

1. Requires the exact supported 310.9.1 provider/fingerprint.
2. Requires one of 16/32/48/64 display-pixel thresholds.
3. Selects the matching calibrated scatter cubin by provider source fingerprint, slot size and threshold.
4. Rewrites exactly one kernel role.
5. Does not allocate or redirect the Reno/legacy warp fatbin.
6. Does not replace inpaint or inpaint-decision kernels.
7. Shares the existing all-or-nothing MFG gate transaction and rollback policy.
8. Refuses the profile if the local calibrated table is absent or the provider fingerprint does not match.

Configuration:

    [DLSSG]
    AdaMfgQualityMode=5
    AdaMfgMagnitudeThresholdPx=64

The threshold selector is exposed only for mode 5. Warp Blend is disabled in the UI
for Adaptive and magnitude-only profiles.

## Validation completed

The following passed on STE-PC / RTX 4090:

- calibrated profile helper test
- source-only compile with the local magnitude table deliberately removed
- full MFG patch/transaction/provider/ceiling/PTX/method/flip-metering suite
- runtime image planning for 16/32/48/64 px
- mode-5 clean rollback, incomplete rollback and bad-fingerprint cases
- actual CUDA driver load and `Kernel_EstimateIntermMvecsScatter` function lookup for all four calibrated cubins
- inherited finished-picture ownership test
- inherited read-only MFG latency analysis test
- inherited HDR/UI diagnostics test
- inherited Streamline NR hook regression
- Release/x64 RTX40-MFG C++ compile and link

The repository's existing Release post-build packaging commands still fail after
linking when the worktree path contains spaces. That is the known unquoted-path
packaging issue, not a source/link failure.

## Remaining gate

**No threshold has yet been visually accepted as superior.**

The next step is a matched Miles gameplay A/B. Hold constant:

- provider/runtime
- FG multiplier
- scene and camera route
- graphics settings
- NRSTAB state (if used, keep the same K/2 px settings)
- Warp Blend off
- capture conditions

Compare the native/current engine control against mode 5 and the calibrated
threshold arms. Record artifact behavior rather than promoting a threshold based on
the offline calibration alone.

RDR2/PureDark and TLOU2/Akane remain separate ownership/integration targets and are
not certified by this Miles profile.
