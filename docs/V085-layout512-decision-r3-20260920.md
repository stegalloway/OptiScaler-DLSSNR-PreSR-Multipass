# G1a revision 3 — frozen 512-byte layout decision

20 September 2026. Written before the first candidate production edit. User selected expansion after reviewing the three alternatives. Base: `0dfd181a4e5e3bd0cdf9b5a2e75530fc616ad0b6`.

## Authorization and boundaries

Select append-only expansion of `DlssNrConstants`: preserve upstream bytes 0–239, append five scalar fields at 240/244/248/252/256, retain `alignas(256)`, resulting C++ size and descriptor CBV range 512. No new aliases, buffer binding or root signature. A separate NRSTAB buffer remains a named fallback only after measured evidence and a separately documented decision.

This authorizes an isolated **layout-only prerequisite candidate**, shader rebuilds and controlled CPU/software-GPU/Vulkan layout and behaviour tests. It does not authorize the full NRSTAB port, ShadowFloor/exposure feature integration, Reno changes, FG hardening implementation, a gameplay deployment or main promotion. Untouched upstream gameplay acceptance still precedes an integrated NRSTAB game candidate. G2a submitted-completion/readback safety is independent and unchanged; it must not be bypassed because G1a passes.

The existing revision-2 document is backed up at `09_ARCHIVE/DOCUMENTATION-BACKUPS/V085-LAYOUT512-R3-20260920-172239`, SHA-256 `AD305BFA877207E7DDB25673433F9BE5492B96B43D06649AF3855B068C9EC1A4`. Earlier preparation manifests remain historical, not overwritten.

## Frozen offset map

Offsets are bytes from b0 / the C++ object start. Every scalar is four bytes. Array packing is specified below; implicit C++ tail padding is not a semantic field. The machine-readable sibling `V085-layout512-offset-map-r3-20260920.json` has the same map and is an immutable test oracle, not regenerated from the edited candidate.

| Field | C++ type | Offset | Bytes | Origin |
| --- | --- | ---: | ---: | --- |
| Mode | uint32_t | 0 | 4 | upstream |
| WhitePoint | float | 4 | 4 | upstream |
| Width | uint32_t | 8 | 4 | upstream |
| Height | uint32_t | 12 | 4 | upstream |
| TransferStrength | float | 16 | 4 | upstream |
| ColourStrength | float | 20 | 4 | upstream |
| DebugView | uint32_t | 24 | 4 | upstream |
| MaxRatio | float | 28 | 4 | upstream |
| Passthrough | uint32_t | 32 | 4 | upstream |
| MvScaleX | float | 36 | 4 | upstream |
| MvScaleY | float | 40 | 4 | upstream |
| GuideWidth | uint32_t | 44 | 4 | upstream |
| GuideHeight | uint32_t | 48 | 4 | upstream |
| CompareMode | uint32_t | 52 | 4 | upstream |
| CompareSplit | float | 56 | 4 | upstream |
| CompareZoom | float | 60 | 4 | upstream |
| CompareSwap | uint32_t | 64 | 4 | upstream |
| Transfer | uint32_t | 68 | 4 | upstream |
| DebugScale | float | 72 | 4 | upstream |
| ReversibleMode | uint32_t | 76 | 4 | upstream |
| ApplyModel | uint32_t | 80 | 4 | upstream |
| Reserved | uint32_t | 84 | 4 | upstream |
| ResidualScale | float | 88 | 4 | upstream |
| SkinProtection | uint32_t | 92 | 4 | upstream |
| ShowSkinMask | uint32_t | 96 | 4 | upstream |
| SkinDetail | float | 100 | 4 | upstream |
| SkinColour | float | 104 | 4 | upstream |
| EnvironmentDetail | float | 108 | 4 | upstream |
| EnvironmentColour | float | 112 | 4 | upstream |
| ResidualBlend | float | 116 | 4 | upstream |
| ResidualHistoryValid | uint32_t | 120 | 4 | upstream |
| ResidualMotionBaseX | uint32_t | 124 | 4 | upstream |
| ResidualMotionBaseY | uint32_t | 128 | 4 | upstream |
| ReplaceDetailStrength | float | 132 | 4 | upstream |
| ModelWorkScale | float | 136 | 4 | upstream |
| ResidualConfidenceSensitivity | float | 140 | 4 | upstream |
| ExposureMode | uint32_t | 144 | 4 | upstream |
| PreExposure | float | 148 | 4 | upstream |
| ExposureTrim | float | 152 | 4 | upstream |
| ExposureProtection | float | 156 | 4 | upstream |
| ExposureAnchorCount | uint32_t | 160 | 4 | upstream |
| ExposureSourceWidth | uint32_t | 164 | 4 | upstream |
| ExposureSourceHeight | uint32_t | 168 | 4 | upstream |
| ExposurePadding | uint32_t | 172 | 4 | upstream |
| ExposureAnchors[16] | float | 176 | 64 | upstream |
| ResidualMotionSign | float | 240 | 4 | NRSTAB |
| ResidualFrameWidth | uint32_t | 244 | 4 | NRSTAB |
| ResidualFrameHeight | uint32_t | 248 | 4 | NRSTAB |
| ResidualOutputWidth | uint32_t | 252 | 4 | NRSTAB |
| ResidualOutputHeight | uint32_t | 256 | 4 | NRSTAB |
| Tail padding | implicit, no semantic field | 260 | 252 | alignment to 512 |

The exposure anchor array is 16 consecutive CPU floats: element i is at `176+4*i`, i=0..15. HLSL uses four float4s at 176/192/208/224, with components at +0/+4/+8/+12. The last anchor occupies 236–239; **there is no gap before NRSTAB at 240**. The residual shader's current declaration ends after its field at 140, so it must add matching unused exposure fields for bytes 144–175 and anchor registers for bytes 176–239 before declaring NRSTAB. Do not append NRSTAB at that shader's old shorter end.

A full HLSL declaration through offset 259 occupies 17 sixteen-byte registers (272 bytes); the backing C++ object and CBV are 512 bytes. Reflection need not report 512 bytes of actively declared shader fields, but no field may exceed the backing range. Add explicit packoffsets for the new fields (c15.x/y/z/w and c16.x). Preserve finished-colour's existing meaningful 0–31 prefix and represent any intervening unused fields explicitly if extending its declaration. Do not change its meaning or introduce ShadowFloor as part of this layout prerequisite.

## Frozen test amendments and outcomes

1. Keep the production size/offset assertions, changing size to 512 and adding alignment and **every mapped field** assertion; preserve all upstream offsets.
2. In `nr_finished_color_smoke.cpp`, `nr_replace_detail_smoke.cpp`, `nr_residual_rr_smoke.cpp`, change only the expected size 256→512; keep existing behavioural and offset checks intact. No weakening, deletion, new skips or altered expected images to make the expansion pass.
3. Run the selected unchanged shader suites on the pristine upstream control before candidate tests: ordinary/skin, exposure, replacement-detail, residual-RR and finished-colour. Re-run identical behavioural tests on rebuilt candidate shaders. Include Vulkan shared consumers when tools/runtime are available; report an unavailable consumer as unverified, not PASS.
4. Reflect the actual production shader declarations and verify scalar/array offsets against this frozen map. To stop dead-code elimination from hiding unused layout fields, compile a **test-only entry point appended to the real production source** which reads the declaration under test. Do not validate solely against a separately copied synthetic cbuffer.
5. Upload distinguishable sentinel words through a size-derived 512-byte descriptor CBV and verify shader readback of all mapped scalars/array elements, including offsets 252 and **256–259**, across ring slots and repeated writes. Reject wrong offsets/ranges/values; include a deliberate truncated/incorrect-layout negative check which must fail validation. Use explicit fence/event completion before CPU readback; no frame-delay heuristic in this harness. Tests must distinguish DX12 descriptor validation from D3D11/software-shader behaviour coverage.
6. Verify all shared shader outputs are neutral under the layout-only change: compare matched deterministic upstream/candidate output where available, and retain all original shader smoke expectations. Rebuild/generated-file identity and common slot/upload sizing must be recorded. Record tool versions/flags, warnings, failures and skips honestly.
7. No universal performance or game-quality claim. If a controlled performance check finds repeatable material regression from the larger upload, stop acceptance, retain evidence and reconsider the named separate-buffer fallback; do not silently switch design. CPU/WARP observations do not establish RTX/NGX gameplay timing.

G1a passes only for the tested consumers when allocation/binding, field offsets, reflection, sentinel readback and unchanged behavioural suites pass. Compilation alone is insufficient. G1 overall still includes later ShadowFloor alias/behaviour tests; G2/G2a and G3 remain separate pending integration gates. Record the first production edit only after this revision and map are written and hash-frozen.
