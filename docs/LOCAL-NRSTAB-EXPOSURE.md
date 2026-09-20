# Local NRSTAB / PR #1 / exposure-validity candidate

Parent: `93fcfd4fc797e906048c31cf3571849032cc5207`, based on v0.8.5 and the local NRSTAB, ShadowFloor, downloaded-Streamline support and adapted PR #1 lifetime work. The separate FG-hardening component is not included. Existing Retarget and optional PTX choices remain unchanged.

This commit adds only a DX12 exposure-validity guard. A readable texture may contain an old value after a rejected exposure dispatch. Held frames may reuse exposure only after a successful write; a failed calculation clears validity, subsequent held frames retry, and resource release clears validity. Metering equations, anchors, normal successful processing, configuration, shaders and FG are unchanged. This is not exposure caching or an FG/image-quality fix.

`tests/nr_encode/ExposureHoldTests.cpp` includes the existing dispatch/COM fixture and the extracted production EncodeInput body. It tests automatic-meter, automatic-reduction and game-exposure failure after a previous success; no stale publication, retry, recovery, valid hold reuse, source changes and manual fallback. Run the same fixture against the parent body as a negative control, then the candidate. Existing 40 encode assertions remain intact. The other NR encode fixture adds only the new state field; its existing failure assertions are unchanged.

CPU boundary tests do not execute NVIDIA NGX or prove gameplay behaviour. A fresh-game package preserves the original opt-in defaults, ordinary backend DLLs, setup scripts and runtime requirements. NVIDIA NR/FG runtime DLLs remain separately supplied. Generic packaging is not RDR2/PureDark compatibility validation.
