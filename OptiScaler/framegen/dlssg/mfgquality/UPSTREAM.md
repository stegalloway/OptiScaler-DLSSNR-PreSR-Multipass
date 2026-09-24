# Local RenoDX quality research inputs

This directory contains source files from pinned
`mavismmg/MFGAdaUnlock-RenoDx` commit
`4406e4fadf4423afb500d8d9a08d5ace9a148d19` (release 1.1.5).
`LICENSE.txt` is the upstream license for those source files. The host adapter
is `../MfgQuality.h`; its gate/kernel/descriptor writes run through the
existing OptiScaler `MfgUnlock::Transaction`, not the upstream ReShade addon.

The two `*.generated.hpp` files are local, NVIDIA-provider-derived binary
inputs. They are deliberately ignored by Git and must never be committed or
redistributed as source. The local build uses these exact generated headers:

| File | SHA-256 |
| --- | --- |
| `blackwell_cubins.generated.hpp` | `079CC15C8D682D537CF8A720D903EDA493D3C6F87590B6D231371A808F2CBF98` |
| `thin_geometry_cubins.generated.hpp` | `00D1D15B1B6AAB42C5BDFF2D752A524EFBE9D2EE9593D6911EC8B1D72E5438A6` |

Their sole provider input was Miles's `nvngx_dlssg.dll` 310.9.1, SHA-256
`FF6E90EB78B827927DFF5B4ECC6B1C870C2E9BCA29ED9F48C7D348CC9E170B82`.
The pinned ptxas 13.4.92 was SHA-256
`BA758767DA154A6BE6FE798CD906DBC4B69E79FEC782461EF7`.
The thin-geometry generator output and audit are kept outside the checkout at
`06_TEST-RESULTS/RENO-ADAPTIVE-20260924`. The older baseline table came from
the local, prior quality-port worktree. Exact kernel fingerprints are verified
again before any runtime mutation.

Source/build/test success is not a gameplay-quality claim. The quality profile
is opt-in, provider- and architecture-specific, and restart-scoped. Mode 0
uses the previously accepted FG engine unchanged.
