# Maintained branch layout

Pinned upstream baseline: `v0.8.4` / `8802b2b470db0462fa1ed03a125e793a7c06d735`.

- `main` â€” validated v0.8.4 custom baseline with ShadowFloor, NR diagnostic bypass, safe exposure/scanner fixes and RTX40 MFG support.
- `tlou2-shadow-akane` â€” maintained TLOU2 / Akane compatibility branch.
- `rdr2-v0.8.4-known-good` â€” maintained RDR2 / PureDark coexistence branch with OptiScaler NGX SR/NR ownership, private submitted-command-list NR epoch tracking, ShadowFloor and the RTX40 MFG bridge.

The rejected `rdr2-shadow-puredark-mfg` / `999ebf3` line is not maintained. Do not use the generic `[FrameGen] External=true` path for the maintained RDR2 build.

Historical experiments and aliases that are not present in the branch list are not maintained.