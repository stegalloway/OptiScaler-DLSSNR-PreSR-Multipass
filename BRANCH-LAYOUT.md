# Maintained branch layout

Pinned upstream baseline: `v0.8.4` / `8802b2b470db0462fa1ed03a125e793a7c06d735`.

- `main` — master baseline: upstream RTX40 MFG + ShadowFloor + Uncharted NR diagnostic-file bypass + early downloaded Streamline plugin loading for legacy hosts + ScannerFix2 safe UAV-barrier capture, R11/R16 float handling, validated automatic selection, and manual pre-lock anchors.
- `shadowfloor` — alias of `main` for old links.
- `shadowfloor-upstream` — untouched pinned v0.8.4 source.
- `tlou2-shadow-akane` — `main` plus Akane DirectComposition compatibility.
- `rdr2-shadow-puredark-mfg` — `main` plus PureDark coexistence and submitted-command-list NR epoch handling.

The abandoned MFG startup-bootstrap and stale host-max clamp experiments are not part of any maintained branch.
