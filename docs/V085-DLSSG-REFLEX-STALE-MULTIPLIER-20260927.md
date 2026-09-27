# DLSS-G / Reflex stale multiplier after FG-off

Date: 2026-09-27

This records a driver/Streamline pacing bug reproduced in Miles Morales both with and without OptiScaler.
It is not caused by OptiScaler's multiplier override. After DLSS-G has run, disabling frame generation can leave
Reflex pacing using the previous interpolation multiplier.

## Reproduction fingerprint

The cap follows the previous multiplier exactly:

| Active ceiling before FG-off | Previous multiplier | FG-off render cap |
| ---: | ---: | ---: |
| 224 fps | 4X | 56 fps |
| 120 fps | 2X | 60 fps |
| ~225 fps | 6X | 37.5 fps |

The 120 fps case was reproduced with OptiScaler absent. The ~225 / 6X case was reproduced again after DDU,
a clean NVIDIA 617.14 installation, and removal of NVIDIA Profile Inspector. That clean run used G-SYNC and
NVIDIA VSync, with NVIDIA Max Frame Rate disabled.

## What the Reflex test established

After 6X -> FG off, the clean-driver run held at ~37.5 fps. Turning Reflex off immediately restored roughly
102-109 fps. Turning Reflex On + Boost back on immediately restored the ~37.5-38 fps cap, with the Reflex sleep
call again taking about 18.8 ms per rendered frame.

Therefore Reflex Off bypasses the stale pacing state; it does not clear it. Re-enabling Reflex consumes the same
stale multiplier again. Re-sending Reflex sleep settings and ordinary Reflex toggling are not reset mechanisms.

Practical workaround for this configuration:

- Keep NVIDIA Max Frame Rate disabled if its stale divided cap is unwanted. Disabling that explicit limiter clears
  its contribution even after the bad state exists.
- When FG is turned off after MFG use, set in-game Reflex Off for as long as FG remains off.
- When FG is enabled again, the game can enable Reflex as required by DLSS-G.

A process restart is not required by this workaround and is not claimed here as a separately validated reset test.

## Cap-free performance baseline used by later work

The 2026-09-27 controlled baseline used 2248x960 render -> 3360x1440 output, NR on, and NVIDIA Max Frame Rate off.
Approximate inferred real/base rates were:

| Mode | Approximate real/base fps | Note |
| --- | ---: | --- |
| FG off | ~75-100 | scene-dependent normal range |
| 2X | ~53 | cleanly measurable |
| 3X | ~50-53 | cleanly measurable |
| 4X | ~47 | cleanly measurable |
| 5X | ~42-44 | approaching the presentation ceiling |
| 6X | ~37.5 | ceiling-bound; not a natural 6X base-rate measurement |

At 6X the output sat around 224.8-224.9 fps with the 240 Hz G-SYNC/VSync presentation policy. Consequently the
37.5 fps inferred base at 6X is 225 / 6 and must not be used as proof of the natural 6X computational cost.

These numbers supersede earlier FG follow-up readings that were taken while a driver/profile pacing ceiling was
active. Magnitude-profile comparisons and high-motion NR work must use this cap-free baseline, not the contaminated
74-75 fps FG-off plateau from the earlier investigation.

## External matching report

NVIDIA Developer Forums, 2025-07-16:
https://forums.developer.nvidia.com/t/reflex-render-fps-limit-remains-halved-after-shutting-down-dlss-g/339271

That report describes the same class of stale Reflex limit after DLSS-G shutdown. Sending eOff, toggling low
latency, and reloading Reflex did not clear it. Keeping DLSS-G off but alive and presenting for a longer settling
period did avoid the stale limit there. OptiScaler does not implement that keep-alive workaround; the current
low-risk workaround is Reflex Off while FG is off.

## Diagnostic policy

The temporary pacing/teardown traces remain available only under [DLSSG] Diagnostics=true. It is off by default.
This gates SLEEPTRACE, FGDRIVERTRACE, FGORDER and the Miles FPS timing probe. Fullscreen/reject FSTRACE logging was
removed after the investigation.
