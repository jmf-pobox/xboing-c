# scripts/ — Agent Instructions

Shell and Python helpers that orchestrate external tools (X11 capture, sox,
ImageMagick, the Claude vision API, Xvfb). All of them run **outside** the
game binary. Production callers go through a Makefile target — don't invoke
the scripts directly from another script or doc.

## Make-target → script map

| Make target | Script |
|---|---|
| `capture-original` | `capture_original.sh` |
| `golden-screen` / `golden-all` / `golden-bonus` / `golden-bonus-all` | `visual_capture.sh` (mode: original) |
| `modern-screen` / `modern-bonus` / `modern-bonus-all` | `visual_capture.sh` (mode: modern) |
| `visual-check` | `visual_capture.sh` (per screen) + `visual_check.py` |
| `audio-literals` | `audio-literals.sh` |
| `audio-literals-check` | `audio-literals-check.sh` |

## Indirect (script-to-script)

- `visual_check.py` shells out to `llm_compare.py` as the inner LLM-call
  subprocess. `llm_compare.py` has no make wrapper of its own and isn't
  called directly.

## No make wrapper (manual / one-time)

- `run-headless.sh` — Xvfb wrapper for ad-hoc headless runs.
- `convert_xpm_to_png.sh`, `verify_png_conversion.sh`, `convert_au_to_wav.sh`
  — one-time asset migrations. Re-run only if rederiving from `bitmaps/`
  or `sounds/originals/`.

## Adding a new script

1. Wrap it in a Makefile target. The user granted blanket `make *` permission;
   raw `./scripts/foo.sh` invocations from docs or other scripts are not.
2. Prefer extending an existing script (e.g. add a mode to `visual_capture.sh`)
   over creating a sibling. Each new script is another caller path to keep
   coherent.
