# Original 1996 XBoing Reference Captures

Reference PNGs captured from the legacy Xlib binary `original/xboing`
under the snapshot patch added in PR (basket 7 / Phase 1 of the
visual-fidelity screenshot-testing methodology).  See
`docs/research/2026-05-03-visual-fidelity-screenshot-testing.md` for
the design.

## How they were captured

```bash
make original-build           # build original/xboing
make capture-original         # run scripts/capture_original.sh
```

`scripts/capture_original.sh` runs `original/xboing -snapshot N` for
each named state, waits for the binary's `XBOING_SNAPSHOT_READY` line
on stdout, then captures the X11 window with ImageMagick `import`.

## How to regenerate

These captures are committed once and treated as the canonical
reference for L3 SSIM comparison (Phase 3).  Regenerating is a
maintainer action — run `make capture-original`, inspect the diffs,
commit deliberately.

CI does **not** rebuild `original/` and does **not** run the capture
script.  CI consumes only the committed PNGs.  This insulates the
test pipeline from future Xlib API drift.

## Catalogue

| File | Frames | Description |
|---|---|---|
| `presents-early.png` | 1 | Earliest visible frame — flag, earth, "Made in Australia" |
| `presents-mid.png` | 200 | Presents screen mid-animation (still on flag/earth) |
| `intro-stable.png` | 1500 | Intro screen — "JUSTIN KIBELL" credits appearing |
| `intro-late.png` | 2500 | Intro screen — first "X" of XBOING title stamping |

## Future captures

Additional baselines are useful but require state setup beyond the
naturally-reachable "advance N frames from PRESENTS" path.  Candidates
for Phase 1.5 (separate PR):

- gameplay frames at level 1, 5, 80 (requires `-snapshot` extension to
  set `mode = MODE_GAME` and load a level)
- bonus screen at each sub-state
- highscore table
- editor screen
- NoWalls / X2 / X4 active states

These need the snapshot patch to grow a `--snapshot-state STATE`
argument that forces `mode` directly (bypassing keypresses).
