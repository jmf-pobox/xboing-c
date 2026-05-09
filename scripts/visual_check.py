#!/usr/bin/env python3
"""
visual_check.py — Phase 2 driver for LLM-based visual fidelity check.

Reads a YAML manifest of (a, b, expected) pairs.  For each pair:

  1. If `capture:` is present and `b` is missing (or --refresh-modern),
     run the capture script.
  2. Run the LLM comparison via llm_compare.py.
  3. Record verdict, findings, and pass/fail vs. the pair's `expected`.

Aggregates into a single Markdown report at
.tmp/visual-check/report.md and exits nonzero if any pair's actual
verdict does not match its `expected` field.

Usage:
    .tmp/venv/bin/python scripts/visual_check.py [MANIFEST]
        [--refresh-modern]   # force re-capture even when b exists
        [--report PATH]      # default .tmp/visual-check/report.md

Manifest format: see tests/visual-check-manifest.yaml.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

import yaml

REPO_ROOT = Path(__file__).resolve().parent.parent
LLM_COMPARE = REPO_ROOT / "scripts" / "llm_compare.py"
PYTHON = REPO_ROOT / ".tmp" / "venv" / "bin" / "python"
DEFAULT_MANIFEST = REPO_ROOT / "tests" / "visual-check-manifest.yaml"
DEFAULT_REPORT = REPO_ROOT / ".tmp" / "visual-check" / "report.md"


def run_capture(pair: dict) -> None:
    """Run the pair's `capture:` script to produce its `b` image."""
    cap = pair.get("capture")
    if not cap:
        return
    script = cap.get("script")
    args = cap.get("args", [])
    if not script:
        raise ValueError(f"pair {pair['name']}: capture has no 'script'")
    cmd = [str(REPO_ROOT / script)] + [str(a) for a in args]
    print(f"  capturing: {' '.join(cmd)}")
    subprocess.run(cmd, check=True)


def run_compare(pair: dict) -> dict:
    """Run llm_compare.py for the pair and return the parsed JSON."""
    cmd = [
        str(PYTHON),
        str(LLM_COMPARE),
        "--original",
        str(pair["a"]),
        "--modern",
        str(pair["b"]),
        "--screen",
        pair["name"],
        "--run-label",
        "visual-check",
    ]
    result = subprocess.run(cmd, check=False, capture_output=True, text=True)
    if result.returncode != 0:
        return {
            "error": "subprocess failed",
            "returncode": result.returncode,
            "stderr": result.stderr,
        }
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError as e:
        return {"error": f"could not parse llm_compare output: {e}", "raw": result.stdout}


def evaluate_pair(pair: dict, refresh_modern: bool) -> dict:
    """Run one pair and return a result dict for the report."""
    name = pair["name"]
    expected = pair["expected"]
    a_path = REPO_ROOT / pair["a"]
    b_path = REPO_ROOT / pair["b"]

    if not a_path.exists():
        return {
            "name": name,
            "expected": expected,
            "status": "error",
            "detail": f"missing image a: {a_path}",
        }

    needs_capture = pair.get("capture") and (refresh_modern or not b_path.exists())
    if needs_capture:
        try:
            run_capture(pair)
        except subprocess.CalledProcessError as e:
            return {
                "name": name,
                "expected": expected,
                "status": "error",
                "detail": f"capture failed: {e}",
            }

    if not b_path.exists():
        return {
            "name": name,
            "expected": expected,
            "status": "error",
            "detail": f"missing image b: {b_path}",
        }

    response = run_compare(pair)
    if "error" in response:
        return {
            "name": name,
            "expected": expected,
            "status": "error",
            "detail": response.get("error"),
            "extra": response,
        }

    parsed = response.get("parsed") or {}
    verdict = parsed.get("verdict")
    findings = parsed.get("findings", [])

    if verdict not in ("equivalent", "diff"):
        status = "error"
        detail = f"unexpected verdict: {verdict!r}"
    elif verdict == expected:
        status = "pass"
        detail = ""
    else:
        status = "fail"
        detail = f"expected {expected}, got {verdict}"

    return {
        "name": name,
        "expected": expected,
        "verdict": verdict,
        "status": status,
        "detail": detail,
        "findings": findings,
        "elapsed": response.get("elapsed_seconds"),
        "input_tokens": response.get("input_tokens"),
        "output_tokens": response.get("output_tokens"),
    }


def write_report(results: list[dict], report_path: Path, manifest_path: Path) -> None:
    report_path.parent.mkdir(parents=True, exist_ok=True)
    fail_count = sum(1 for r in results if r["status"] == "fail")
    err_count = sum(1 for r in results if r["status"] == "error")
    pass_count = sum(1 for r in results if r["status"] == "pass")
    total_in = sum(r.get("input_tokens") or 0 for r in results)
    total_out = sum(r.get("output_tokens") or 0 for r in results)

    lines = [
        "# Visual-Fidelity Comparison Report",
        "",
        f"**Manifest:** `{manifest_path}`",
        f"**Generated:** {time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())}",
        "",
        f"## Summary: {pass_count} pass, {fail_count} fail, {err_count} error",
        "",
        f"Token totals: {total_in} input / {total_out} output (Opus 4.7).",
        "",
        "| Pair | Expected | Verdict | Status | Findings | Elapsed |",
        "|------|----------|---------|--------|----------|---------|",
    ]
    for r in results:
        elapsed = f"{r.get('elapsed', 0)}s" if r.get("elapsed") is not None else ""
        n_findings = len(r.get("findings") or [])
        verdict = r.get("verdict") or "—"
        lines.append(
            f"| {r['name']} | {r['expected']} | {verdict} | "
            f"**{r['status']}** | {n_findings} | {elapsed} |"
        )

    for r in results:
        if r["status"] == "error":
            lines += ["", f"### ERROR: {r['name']}", "", "```", str(r.get("detail")), "```"]
        elif r["status"] == "fail":
            lines += ["", f"### FAIL: {r['name']}", "", r.get("detail", ""), ""]
            if r.get("findings"):
                lines.append("Findings reported by LLM:")
                lines.append("")
                for f in r["findings"]:
                    lines.append(
                        f"- **[{f.get('severity','?')}] "
                        f"{f.get('category','?')}**: {f.get('description','')}"
                    )

    diff_pairs = [r for r in results if r["status"] == "pass" and r.get("verdict") == "diff"]
    if diff_pairs:
        lines += ["", "## Diff findings (passing pairs where divergence was expected)", ""]
        for r in diff_pairs:
            lines += [f"### {r['name']}", ""]
            for f in r.get("findings") or []:
                lines.append(
                    f"- **[{f.get('severity','?')}] "
                    f"{f.get('category','?')}**: {f.get('description','')}"
                )
            lines.append("")

    report_path.write_text("\n".join(lines) + "\n")


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("manifest", nargs="?", default=str(DEFAULT_MANIFEST), type=Path)
    p.add_argument("--refresh-modern", action="store_true",
                   help="force re-capture of all `b` images even if they exist")
    p.add_argument("--report", default=str(DEFAULT_REPORT), type=Path)
    args = p.parse_args()

    if not args.manifest.exists():
        print(f"ERROR: manifest not found: {args.manifest}", file=sys.stderr)
        return 2

    if not PYTHON.exists():
        print(f"ERROR: managed venv not found at {PYTHON}", file=sys.stderr)
        print("       run: uv venv .tmp/venv && "
              "uv pip install --python .tmp/venv/bin/python anthropic pyyaml",
              file=sys.stderr)
        return 2
    if not LLM_COMPARE.exists():
        print(f"ERROR: {LLM_COMPARE} missing", file=sys.stderr)
        return 2

    manifest = yaml.safe_load(args.manifest.read_text())
    pairs = manifest.get("pairs") or []
    if not pairs:
        print("ERROR: manifest has no pairs", file=sys.stderr)
        return 2

    results: list[dict] = []
    for i, pair in enumerate(pairs, 1):
        print(f"[{i}/{len(pairs)}] {pair.get('name')} "
              f"(expected={pair.get('expected')})")
        result = evaluate_pair(pair, args.refresh_modern)
        marker = {"pass": "✓", "fail": "✗", "error": "!"}.get(result["status"], "?")
        verdict_part = f"verdict={result.get('verdict')}" if result.get("verdict") else ""
        print(f"  {marker} {result['status']} {verdict_part} "
              f"{result.get('detail') or ''}")
        results.append(result)

    write_report(results, args.report, args.manifest)
    print(f"\nReport: {args.report}")

    fail_or_err = sum(1 for r in results if r["status"] in ("fail", "error"))
    return 1 if fail_or_err else 0


if __name__ == "__main__":
    sys.exit(main())
