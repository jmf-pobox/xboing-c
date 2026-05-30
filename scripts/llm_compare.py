#!/usr/bin/env python3
"""
llm_compare.py — slice-experiment helper.

Compares two PNGs (an original/xboing reference vs. modern xboing
capture) using the Anthropic API.  Throwaway scaffolding for the
LLM-comparison thin slice (mission m-2026-05-07-001).  Real Phase 2
will replace this with proper structured output validation, batching,
and retry.

Usage:
    .tmp/venv/bin/python scripts/llm_compare.py \\
        --original PATH \\
        --modern   PATH \\
        --screen   NAME \\
        [--temperature 0]   # 0 for consistency; 1 for variance sample
        [--run-label N]     # tagged in output

Environment:
    ANTHROPIC_API_KEY — required.
"""
from __future__ import annotations

import argparse
import base64
import json
import os
import subprocess
import sys
import time
from pathlib import Path

import anthropic


def resolve_api_key() -> str | None:
    """Resolve the Anthropic API key from environment or gnome-keyring.

    Order:
      1. $ANTHROPIC_API_KEY (explicit override).
      2. `secret-tool lookup service anthropic` (gnome-keyring; the
         "Passwords and Keys" GUI stores entries here).
    Returns None if neither produced a non-empty value.
    """
    env = os.environ.get("ANTHROPIC_API_KEY")
    if env:
        return env
    try:
        result = subprocess.run(
            ["secret-tool", "lookup", "service", "anthropic"],
            check=False,
            capture_output=True,
            text=True,
            timeout=5,
        )
    except (FileNotFoundError, subprocess.TimeoutExpired):
        return None
    if result.returncode != 0:
        return None
    key = result.stdout.strip()
    return key or None

MODEL = "claude-opus-4-7"

PROMPT_TEMPLATE = """\
Compare these two screenshots of the same XBoing game state.

The first image is from the canonical 1996 X11 binary (original/xboing).
The second image is from the SDL2 modernization we are testing for
visual fidelity.

Screen identifier: {screen_name}

Report concrete visual differences in:
- Layout (positions of UI elements relative to each other)
- Alignment (right-anchored vs. left-anchored, stride between repeated elements)
- Sprites (which sprite is shown — color, type, animation frame)
- Colors (background, border, accent — but not subtle font color shifts)
- Text positioning (where text lands; not glyph-level rendering)
- Animation state (which frame of an animation is shown)

Out of scope (do not report these):
- Font hinting / anti-aliasing differences
- 1-2 pixel shifts that a player would not notice
- Color quantization differences from XPM-to-PNG conversion
- Cursor or window decoration artifacts (title bars, window borders
  added by the desktop environment)

Output ONLY a JSON object with this shape (no prose around it):

{{
  "verdict": "equivalent" | "diff",
  "findings": [
    {{
      "category": "layout|alignment|sprite|color|text|animation",
      "description": "what is different",
      "severity": "trivial|minor|major"
    }}
  ]
}}

If verdict is "equivalent", findings should be an empty list.
"""


def encode_image(path: Path) -> dict:
    """Read a PNG and return an Anthropic image content block."""
    with path.open("rb") as f:
        data = f.read()
    return {
        "type": "image",
        "source": {
            "type": "base64",
            "media_type": "image/png",
            "data": base64.standard_b64encode(data).decode(),
        },
    }


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--original", required=True, type=Path)
    p.add_argument("--modern", required=True, type=Path)
    p.add_argument("--screen", required=True)
    p.add_argument("--temperature", type=float, default=0.0)
    p.add_argument("--run-label", default="unlabeled")
    args = p.parse_args()

    api_key = resolve_api_key()
    if not api_key:
        print(
            "ERROR: no Anthropic API key found.\n"
            "Either export ANTHROPIC_API_KEY, or store it in gnome-keyring:\n"
            "  secret-tool store --label='Anthropic API key' service anthropic",
            file=sys.stderr,
        )
        return 2

    for path in (args.original, args.modern):
        if not path.exists():
            print(f"ERROR: image not found: {path}", file=sys.stderr)
            return 2

    client = anthropic.Anthropic(api_key=api_key)

    prompt = PROMPT_TEMPLATE.format(screen_name=args.screen)
    content = [
        {"type": "text", "text": prompt},
        encode_image(args.original),
        encode_image(args.modern),
    ]

    started = time.monotonic()
    timestamp = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
    # claude-opus-4-7 deprecates the temperature parameter — pinning is
    # not supported.  Per gjm B-2 the intent is verdict consistency
    # across runs; we test that empirically by running the same input
    # multiple times and comparing the verdict field.  --temperature
    # is accepted for forward-compat with non-Opus models but ignored
    # if 0.0 (the default).
    api_kwargs = {
        "model": MODEL,
        "max_tokens": 2048,
        "messages": [{"role": "user", "content": content}],
    }
    if args.temperature > 0.0:
        api_kwargs["temperature"] = args.temperature
    try:
        response = client.messages.create(**api_kwargs)
    except anthropic.APIError as e:
        # Per gjm review NB-1: print HTTP context on failure so the
        # findings doc can distinguish API noise from real LLM jitter.
        print(
            json.dumps(
                {
                    "run_label": args.run_label,
                    "timestamp": timestamp,
                    "screen": args.screen,
                    "temperature": args.temperature,
                    "error": str(e),
                    "error_type": type(e).__name__,
                }
            )
        )
        return 1

    elapsed = time.monotonic() - started

    raw_text = "".join(
        b.text for b in response.content if b.type == "text"
    )

    # Per gjm review NB-2: assert top-level verdict key when parseable.
    # Throwaway slice — no schema enforcement beyond that.
    parsed: dict | None = None
    parse_error: str | None = None
    try:
        parsed = json.loads(raw_text)
        if "verdict" not in parsed:
            parse_error = "no 'verdict' key in parsed JSON"
    except json.JSONDecodeError as e:
        parse_error = f"JSON parse failed: {e}"

    print(
        json.dumps(
            {
                "run_label": args.run_label,
                "timestamp": timestamp,
                "screen": args.screen,
                "temperature": args.temperature,
                "model": MODEL,
                "elapsed_seconds": round(elapsed, 2),
                "input_tokens": response.usage.input_tokens,
                "output_tokens": response.usage.output_tokens,
                "raw_text": raw_text,
                "parsed": parsed,
                "parse_error": parse_error,
            },
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
