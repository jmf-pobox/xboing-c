# Project History

This directory is a running case study of one thing: **what happens when you
try to modernize a real, load-bearing legacy codebase using LLM agents.**

The subject is XBoing — Justin C. Kibell's 1993–1996 X11 breakout game. It
was written by one person over three years on Sun and SGI workstations, sat
unmaintained for two decades, and became the test rig for this experiment:
port it to modern Linux + SDL2, preserve the 1996 feel exactly, and let the
agents do the work under a principal-engineer standard.

The goal of the archive here isn't nostalgia. It's evidence. How much of
this can an LLM actually drive? How fast does it converge? What kinds of
mistakes recur? Where does the human still have to step in?

## What's in here

| File | What it is |
|------|------------|
| [`chronology.md`](chronology.md) | Rolling per-session log of the modernization: pre-Claude manual foundation → Session 1–4 → onward. Bead counts, PR counts, cumulative completion. Grows over time. |
| [`2026-02-26-modernization-milestone.md`](2026-02-26-modernization-milestone.md) | Point-in-time snapshot at the first "modernization complete" milestone (91 beads closed, all 14 epics done, ~43,500 lines of modern C, ~64 merged PRs across 7 days of active work). |

## Conventions for future entries

- **Milestone snapshots**: dated ISO prefix — `YYYY-MM-DD-<slug>.md`. Freeze
  the numbers at that moment; don't retroactively edit. If the picture
  changes, write a new entry.
- **Rolling documents**: unversioned name (`chronology.md`). Append; keep
  the older sections intact.
- **Retrospectives**: `YYYY-MM-DD-retro-<topic>.md`. What worked, what
  didn't, and what a second attempt would change. Cite the specific commits
  and PRs.
- **Numbers**: always source them from `git log`, `bd stats`, or `cloc`.
  Don't estimate. If you have to estimate, say so.

## Why this exists

Two audiences. First: future contributors (agent or human) trying to
understand how the codebase got here and why certain decisions look the way
they do. The commit log tells them *what* changed; this directory tells
them *why the shape of the work was possible*.

Second: anyone else running the same experiment on a different legacy
codebase. This project is one data point. The more honest we are about the
details — including the wrong turns and the fragile bits — the more useful
the data point becomes.
