# Audio Volume Modulation Audit

**Date:** 2026-06-28
**Bead:** xboing-c-gyy
**Author:** jck (delegated by COO)

## Context

The original Sun/SGI backend modulated volume per `playSoundFile(name, volume)`
call (`original/audio/SUNaudio.c:243`), producing a dynamic range of roughly
10× between the softest bounce (10) and the loudest catastrophe (99-100). The
modern port wires every non-warp call through `sdl2_audio_play(ctx, name)`,
which plays at master volume and collapses that entire range to a single
loudness level. PR #154 restored per-key warp volume for `tone` at
`src/game_input.c:173`. This audit catalogs every remaining `sdl2_audio_play`
call site and specifies the volume each should receive so jdc can migrate them
to `sdl2_audio_play_at_percent`.

---

## 1. Original-side catalog

### ammo

- volume 30 — BULLET_BLK hit (limited ammo pickup)
  - `original/blocks.c:776`
- volume 70 — MAXAMMO_BLK hit (full ammo pickup)
  - `original/blocks.c:780`

### applause

- volume 70 — level cleared, entering bonus screen
  - `original/level.c:413`
- volume 80 — bonus screen sequence complete
  - `original/bonus.c:600`

### ball2ball

- volume 90 — two live balls collide
  - `original/ball.c:1335`

### balllost

- volume 99 — ball exits play area, lives decremented
  - `original/level.c:477`

### boing

- volume 10 — wall bounce (all three walls)
  - `original/ball.c:1049` — left wall
  - `original/ball.c:1067` — right wall
  - `original/ball.c:1085` — top wall
- volume 50 — high-score screen transition (attract mode navigation)
  - `original/keys.c:300`

### bomb

- volume 50 — BOMB_BLK hit
  - `original/blocks.c:772`

### bonus

- volume 50 — TIMER_BLK hit (extra time pickup)
  - `original/blocks.c:834`
- volume 20 — editor: block placement/removal confirmations
  - `original/editor.c:441`, `original/editor.c:455`, `original/editor.c:531`, `original/editor.c:544`
- volume 50 — bonus screen: key tally line
  - `original/bonus.c:366`

### buzzer

- volume 70 — time bonus reaches zero
  - `original/level.c:147`

### click

- volume 70 — dialogue: backspace / cancel keystroke
  - `original/dialogue.c:295`
- volume 99 — gun trigger with zero ammo
  - `original/gun.c:511`

### ddloo

- volume 99 — EXTRABALL_BLK hit
  - `original/blocks.c:800`

### Doh1

- volume 80 — bonus screen: bullet lost line
  - `original/bonus.c:315`

### Doh2

- volume 80 — bonus screen: lives lost line
  - `original/bonus.c:421`

### Doh3

- volume 80 — bonus screen: time lost line
  - `original/bonus.c:450`

### Doh4

- volume 80 — bonus screen: score tally header
  - `original/bonus.c:292`, `original/bonus.c:520`

### evillaugh

- volume 50 — editor: load/save action
  - `original/editor.c:394`
- volume 99 — DEATH_BLK hit
  - `original/blocks.c:842`

### game_over

- volume 99 — game ended (all lives lost or level file missing)
  - `original/level.c:458`
- volume 100 — main.c end-of-attract-cycle variant
  - `original/main.c:715`

### gate

- volume 50 — highscore screen exits to preview (attract cycle)
  - `original/highscore.c:492`
- volume 99 — BONUSX2_BLK / BONUSX4_BLK / BONUS_BLK hit
  - `original/blocks.c:814`

### hithere

- volume 100 — eye-dude enters play area
  - `original/eyedude.c:280`

### hypspc

- volume 99 — HYPERSPACE_BLK hit
  - `original/blocks.c:850`

### intro

- volume 40 — presents/credits screen begins
  - `original/presents.c:234`

### key

- volume 50 — bonus screen: key tally line (key collected)
  - `original/bonus.c:473`
- volume 60 — presents screen: key credit reveals
  - `original/presents.c:423`, `original/presents.c:458`, `original/presents.c:493`
- volume 70 — dialogue: character typed
  - `original/dialogue.c:338`

### looksbad

- volume 80 — preview screen: 33% chance on start (comical commentary)
  - `original/preview.c:156`

### metal

- volume 99 — BLACK_BLK hit
  - `original/blocks.c:846`

### mgun

- volume 99 — MGUN_BLK hit
  - `original/blocks.c:804`

### ouch

- volume 99 — ROAMER_BLK hit
  - `original/blocks.c:796`

### paddle

- volume 50 — ball hits paddle
  - `original/ball.c:1096`

### ballshot

- volume 50 — bullet kills a ball (gun mode, ball hit)
  - `original/gun.c:288`

### ping

- volume 70 — presents screen: sparkle/ping visual effect
  - `original/presents.c:372`

### shark

- volume 50 — instructions screen finishes cycling
  - `original/inst.c:210`

### shoot

- volume 80 — bullet spawned (single-shot gun fires)
  - `original/gun.c:215`

### shotgun

- volume 50 — multi-shot gun fires
  - `original/gun.c:504`

### spring

- volume 80 — MULTIBALL_BLK hit
  - `original/blocks.c:830`

### stamp

- volume 90 — presents screen: credit stamp animation
  - `original/presents.c:331`, `original/presents.c:342`

### sticky

- volume 50 — editor: sticky block placed/removed
  - `original/editor.c:688`, `original/editor.c:772`
- volume 90 — STICKY_BLK hit
  - `original/blocks.c:838`

### supbons

- volume 80 — eye-dude killed (super bonus triggered)
  - `original/eyedude.c:383`
- volume 80 — bonus screen: super bonus line
  - `original/bonus.c:334`

### toggle

- volume 50 — G key (control mode toggle) or H key (highscore navigation)
  - `original/main.c:393`, `original/main.c:620`, `original/main.c:635`

### tone

- volumes 10–90 in steps of 10 — warp speed keys 1–9
  - `original/main.c:750–806`
- volume 40 — dialogue: text buffer overflow (input field full)
  - `original/dialogue.c:301`

### touch

- volume 99 — normal color block hit (RED/GREEN/BLUE/TAN/PURPLE/YELLOW/COUNTER/RANDOM/DROP)
  - `original/blocks.c:792`

### wallsoff

- volume 99 — WALLOFF_BLK hit
  - `original/blocks.c:808`

### warp

- volume 50 — REVERSE_BLK hit (block volume)
  - `original/blocks.c:818`
- volume 50 — key-edit screen finishes
  - `original/keysedit.c:257`

### whizzo

- volume 50 — demo or preview screen finishes cycling
  - `original/demo.c:255`, `original/preview.c:168`

### whoosh

- volume 50 — intro screen finishes cycling
  - `original/intro.c:352`
- volume 70 — presents screen: whoosh transition effect
  - `original/presents.c:519`

### wzzz

- volume 50 — PAD_EXPAND_BLK hit (block volume)
  - `original/blocks.c:826`
- volume 50 — editor: expand-paddle block placed
  - `original/editor.c:653`

### wzzz2

- volume 50 — PAD_SHRINK_BLK hit (block volume)
  - `original/blocks.c:822`
- volume 50 — editor: shrink-paddle block placed
  - `original/editor.c:734`

### youagod

- volume 99 — new high-score rank 1 achieved
  - `original/level.c:437`

---

## 2. Modern-side catalog

All `sdl2_audio_play` calls, grouped by file. The warp-tone site at
`src/game_input.c:173` is excluded — it already uses `sdl2_audio_play_at_percent`.

### src/ball_system.c

Sounds emitted via `on_sound` callback → `ball_cb_on_sound` →
`sdl2_audio_play` at `src/game_callbacks.c:324`.

- line 490 — "boing" — left wall bounce
- line 510 — "boing" — right wall bounce
- line 530 — "boing" — top wall bounce
- line 757 — "ball2ball" — ball-to-ball collision

Note: `ball_system.c` also calls `on_sound("paddle", ...)` at line 545 but that fires `BALL_EVT_PADDLE_HIT` which is handled separately in `ball_cb_on_event` at `src/game_callbacks.c:364`.

### src/game_callbacks.c

- line 77 — `play_block_hit_sound` → `block_sound_name(block_type)` — block hit, sound name varies by type
- line 324 — `ball_cb_on_sound(name, ...)` — relay from ball_system (boing, ball2ball)
- line 364 — "paddle" — ball hits paddle (BALL_EVT_PADDLE_HIT)
- line 518 — `gun_cb_on_sound(name, ...)` — relay from gun_system (shoot, ballshot, shotgun, click)
- line 666 — `bonus_cb_on_sound(name, ...)` — relay from bonus_system (Doh1–4, supbons, bonus, key, applause)
- line 779 — `sfx_cb_on_sound(name, ...)` — relay from sfx_system (sfx_system never calls this in current code)
- line 821 — `eyedude_cb_on_sound(name, ...)` — relay from eyedude_system (hithere, supbons)
- line 882 — `editor_cb_on_sound(name, volume, ...)` — relay from editor_system; **volume is received but discarded** (evillaugh, bonus, wzzz, wzzz2, sticky)

### src/game_input.c

- line 205 — "toggle" — H key (highscore navigation in attract mode)
- line 240 — "toggle" — G key (control mode toggle)

### src/game_modes.c

- line 165 — "buzzer" — time bonus expired
- line 396 — `presents_system_get_sound().name` — presents screen sound relay (intro, stamp, ping, key, whoosh)
- line 447 — `intro_system_get_sound().name` — intro screen sound relay (whoosh)
- line 514 — `intro_system_get_sound().name` — instruct screen sound relay (shark)
- line 570 — `demo_system_get_sound().name` — demo screen sound relay (whizzo, looksbad)
- line 610 — `demo_system_get_sound().name` — preview screen sound relay (whizzo, looksbad)
- line 1163 — `dialogue_system_get_sound().name` — dialogue sound relay (click, key, tone)

### src/game_rules.c

- line 160 — "bomb" — dynamite bonus event: clears all blocks of random color
- line 200 — "game_over" — level file missing, ending game
- line 223 — "game_over" — level file corrupt, ending game
- line 298 — "game_over" — all lives lost
- line 309 — "balllost" — one life lost, ball reset to paddle
- line 337 — "applause" — level cleared, transitioning to bonus screen

---

## 3. Mapping table

| Modern call site | Sound | Recommended % | Original cite(s) | Notes |
|---|---|---|---|---|
| `src/game_callbacks.c:77` (BOMB_BLK) | `bomb` | 50 | `original/blocks.c:772` | BOMB_BLK hit |
| `src/game_callbacks.c:77` (BULLET_BLK) | `ammo` | 30 | `original/blocks.c:776` | BULLET_BLK hit — **blocked**: block_sound_name collapses BULLET_BLK and MAXAMMO_BLK; see §5 |
| `src/game_callbacks.c:77` (MAXAMMO_BLK) | `ammo` | 70 | `original/blocks.c:780` | MAXAMMO_BLK hit — **blocked**: same collapse issue |
| `src/game_callbacks.c:77` (RED/GREEN/BLUE/TAN/PURPLE/YELLOW/COUNTER/RANDOM/DROP) | `touch` | 99 | `original/blocks.c:792` | normal color block hit |
| `src/game_callbacks.c:77` (ROAMER_BLK) | `ouch` | 99 | `original/blocks.c:796` | roamer block hit |
| `src/game_callbacks.c:77` (EXTRABALL_BLK) | `ddloo` | 99 | `original/blocks.c:800` | extra ball pickup |
| `src/game_callbacks.c:77` (MGUN_BLK) | `mgun` | 99 | `original/blocks.c:804` | machine gun pickup |
| `src/game_callbacks.c:77` (WALLOFF_BLK) | `wallsoff` | 99 | `original/blocks.c:808` | walls-off pickup |
| `src/game_callbacks.c:77` (BONUSX2/X4/BONUS) | `gate` | 99 | `original/blocks.c:814` | bonus multiplier pickup |
| `src/game_callbacks.c:77` (REVERSE_BLK) | `warp` | 99 | `original/blocks.c:818` | reverse control pickup |
| `src/game_callbacks.c:77` (PAD_SHRINK_BLK) | `wzzz2` | 99 | `original/blocks.c:822` | paddle shrink pickup |
| `src/game_callbacks.c:77` (PAD_EXPAND_BLK) | `wzzz` | 99 | `original/blocks.c:826` | paddle expand pickup |
| `src/game_callbacks.c:77` (MULTIBALL_BLK) | `spring` | 80 | `original/blocks.c:830` | multiball pickup |
| `src/game_callbacks.c:77` (TIMER_BLK) | `bonus` | 50 | `original/blocks.c:834` | extra time pickup |
| `src/game_callbacks.c:77` (STICKY_BLK) | `sticky` | 90 | `original/blocks.c:838` | sticky ball pickup |
| `src/game_callbacks.c:77` (DEATH_BLK) | `evillaugh` | 99 | `original/blocks.c:842` | death block hit |
| `src/game_callbacks.c:77` (BLACK_BLK) | `metal` | 99 | `original/blocks.c:846` | solid wall block hit |
| `src/game_callbacks.c:77` (HYPERSPACE_BLK) | `hypspc` | 99 | `original/blocks.c:850` | hyperspace pickup |
| `src/game_callbacks.c:324` ("boing") | `boing` | 10 | `original/ball.c:1049,1067,1085` | wall bounce — all three walls |
| `src/game_callbacks.c:364` ("paddle") | `paddle` | 50 | `original/ball.c:1096` | ball hits paddle |
| `src/game_callbacks.c:518` ("shoot") | `shoot` | 80 | `original/gun.c:215` | single bullet fired |
| `src/game_callbacks.c:518` ("ballshot") | `ballshot` | 50 | `original/gun.c:288` | bullet kills a ball |
| `src/game_callbacks.c:518` ("shotgun") | `shotgun` | 50 | `original/gun.c:504` | multi-shot fires |
| `src/game_callbacks.c:518` ("click") | `click` | 99 | `original/gun.c:511` | trigger pulled with 0 ammo |
| `src/game_callbacks.c:666` ("Doh4") | `Doh4` | 80 | `original/bonus.c:292,520` | bonus score header |
| `src/game_callbacks.c:666` ("Doh1") | `Doh1` | 80 | `original/bonus.c:315` | bullet lost line |
| `src/game_callbacks.c:666` ("supbons") | `supbons` | 80 | `original/bonus.c:334` | super bonus line |
| `src/game_callbacks.c:666` ("bonus") | `bonus` | 50 | `original/bonus.c:366` | key tally line |
| `src/game_callbacks.c:666` ("Doh2") | `Doh2` | 80 | `original/bonus.c:421` | lives lost line |
| `src/game_callbacks.c:666` ("Doh3") | `Doh3` | 80 | `original/bonus.c:450` | time lost line |
| `src/game_callbacks.c:666` ("key") | `key` | 50 | `original/bonus.c:473` | key collected line |
| `src/game_callbacks.c:666` ("applause") | `applause` | 80 | `original/bonus.c:600` | bonus sequence complete |
| `src/game_callbacks.c:821` ("hithere") | `hithere` | 100 | `original/eyedude.c:280` | eye-dude enters |
| `src/game_callbacks.c:821` ("supbons") | `supbons` | 80 | `original/eyedude.c:383` | eye-dude killed |
| `src/game_callbacks.c:882` (evillaugh) | `evillaugh` | 50 | `original/editor.c:394` | editor load/save; volume already passed by editor_system, just wire it |
| `src/game_callbacks.c:882` (bonus) | `bonus` | 20 | `original/editor.c:441,455,531,544` | editor block confirmation; volume already passed |
| `src/game_callbacks.c:882` (wzzz) | `wzzz` | 50 | `original/editor.c:653` | editor expand block placed; volume already passed |
| `src/game_callbacks.c:882` (wzzz2) | `wzzz2` | 50 | `original/editor.c:734` | editor shrink block placed; volume already passed |
| `src/game_callbacks.c:882` (sticky) | `sticky` | 50 | `original/editor.c:688,772` | editor sticky block placed; volume already passed |
| `src/game_callbacks.c:757` ("ball2ball") | `ball2ball` | 90 | `original/ball.c:1335` | ball-to-ball collision |
| `src/game_input.c:205` | `toggle` | 50 | `original/main.c:620,635` | H key: highscore navigation |
| `src/game_input.c:240` | `toggle` | 50 | `original/main.c:393` | G key: control mode toggle |
| `src/game_modes.c:165` | `buzzer` | 70 | `original/level.c:147` | time bonus expired |
| `src/game_modes.c:396` ("intro") | `intro` | 40 | `original/presents.c:234` | presents screen begins |
| `src/game_modes.c:396` ("stamp") | `stamp` | 90 | `original/presents.c:331,342` | presents credit stamp |
| `src/game_modes.c:396` ("ping") | `ping` | 70 | `original/presents.c:372` | presents sparkle |
| `src/game_modes.c:396` ("key") | `key` | 60 | `original/presents.c:423,458,493` | presents key credit |
| `src/game_modes.c:396` ("whoosh") | `whoosh` | 70 | `original/presents.c:519` | presents transition |
| `src/game_modes.c:447` ("whoosh") | `whoosh` | 50 | `original/intro.c:352` | intro screen cycle end |
| `src/game_modes.c:514` ("shark") | `shark` | 50 | `original/inst.c:210` | instruct screen cycle end |
| `src/game_modes.c:570` ("whizzo") | `whizzo` | 50 | `original/demo.c:255` | demo cycle end |
| `src/game_modes.c:570` ("looksbad") | `looksbad` | 80 | `original/preview.c:156` | demo: 33% comical start |
| `src/game_modes.c:610` ("whizzo") | `whizzo` | 50 | `original/preview.c:168` | preview cycle end |
| `src/game_modes.c:610` ("looksbad") | `looksbad` | 80 | `original/preview.c:156` | preview: 33% comical start |
| `src/game_modes.c:1163` ("click") | `click` | 70 | `original/dialogue.c:295` | dialogue backspace/cancel |
| `src/game_modes.c:1163` ("key") | `key` | 70 | `original/dialogue.c:338` | dialogue character typed |
| `src/game_modes.c:1163` ("tone") | `tone` | 40 | `original/dialogue.c:301` | dialogue input buffer full |
| `src/game_rules.c:160` | `bomb` | 50 | `original/blocks.c:772` | dynamite bonus: uses bomb SFX; no exact original analog for this trigger context — 50 matches BOMB_BLK hit, which is the closest semantic |
| `src/game_rules.c:200,223` | `game_over` | 99 | `original/level.c:458` | level file missing/corrupt |
| `src/game_rules.c:298` | `game_over` | 99 | `original/level.c:458` | all lives lost |
| `src/game_rules.c:309` | `balllost` | 99 | `original/level.c:477` | one life lost |
| `src/game_rules.c:337` | `applause` | 70 | `original/level.c:413` | level cleared (bonus entry) |

---

## 4. Decision rules

These rules are read directly from the 1996 source. I did not invent them.

- **10**: wall bounce ("boing") — softest event, repeated every frame, must not mask game audio
- **20**: editor UI confirmation feedback ("bonus") — lowest-priority UI
- **30**: low-value pickup ("ammo" for BULLET_BLK) — minor event, softer than major pickups
- **40–50**: standard gameplay events — paddle hit, ball/gun fire, menu navigation, normal pickups (bomb 50, toggle 50, warp reverse pickup 99 in blocks but 50 in editor, shotgun 50, ballshot 50, dialogue tone 40)
- **60–70**: UI interaction, timing events — dialogue key 70, click 70, buzzer 70, ping 70, applause 70 on level clear
- **80**: major special events — shoot 80, spring 80, looksbad 80, Doh series 80, supbons 80, applause 80 on bonus complete
- **90**: near-catastrophic — ball2ball 90, stamp 90, sticky 90, ammo for MAXAMMO 70 (upgrade), balllost 99 is catastrophic
- **99–100**: catastrophic — touch/ouch/ddloo/mgun/wallsoff/gate/warp/wzzz/wzzz2/evillaugh/metal/hypspc all at 99 on block hit; game_over 99; hithere 100; game_over variant 100 in main.c

**Inconsistencies found:**

- `applause`: 70 at level clear (`original/level.c:413`), 80 at bonus sequence end (`original/bonus.c:600`). These are different events. Use 70 for `game_rules.c:337` (level clear) and 80 for `game_callbacks.c:666` (bonus complete). Both citations are solid.
- `boing`: 10 for wall bounces, 50 for highscore transition in keys.c. The keys.c call fires once at screen transition, not per-frame — the 50 is deliberate. Modern `keys_system.c` already stores volume 50 for the boing sound (`src/keys_system.c:211-212`), so the relay at `game_modes.c` needs a volume-aware path.
- `whoosh`: 50 in intro.c, 70 in presents.c. Different screens, different volumes. Use 50 for intro relay (game_modes.c:447), 70 for presents relay (game_modes.c:396).
- `key`: 50 in bonus.c, 60 in presents.c, 70 in dialogue.c. Three distinct events with increasing importance. Apply per-event.
- `click`: 70 in dialogue.c (backspace), 99 in gun.c (empty trigger). These relay through different code paths so no ambiguity.
- `gate`: 50 in highscore.c (attract cycle transition), 99 in blocks.c (bonus block pickup). These relay through different code paths so no ambiguity.
- `sticky`: 50 in editor.c (2 calls), 90 in blocks.c. Different code paths.
- `warp`/`wzzz`/`wzzz2`: 50 in editor, 99 in blocks.c. Different code paths.
- `evillaugh`: 50 in editor.c, 99 in blocks.c. Different code paths.
- `bonus`: 20 in editor.c, 50 in blocks.c/bonus.c. Different code paths.

---

## 5. Edge cases

### Sounds in `original/` not called via `sdl2_audio_play` in the modern port

These events are not currently implemented in the modern code and have no call site:

| Sound | Volume | Original cite | Missing event |
|---|---|---|---|
| `gate` | 50 | `original/highscore.c:492` | Highscore screen exit to preview — no sound emitted in modern highscore mode |
| `youagod` | 99 | `original/level.c:437` | New rank-1 highscore — modern port doesn't detect this case |
| `weeek` | — | not in `playSoundFile` grep results | No original call site found; file exists in sounds/ but was never wired |

### BULLET_BLK vs MAXAMMO_BLK volume collapse

`src/block_sound.c:14-15` maps both `BULLET_BLK` and `MAXAMMO_BLK` to the
string "ammo". The original uses volume 30 for BULLET_BLK and 70 for MAXAMMO_BLK
(`original/blocks.c:776,780`). A single `sdl2_audio_play_at_percent(audio, "ammo", X)`
call cannot reproduce both. Fix options for jdc:

1. Extend `block_sound_name` to return a `(name, volume)` pair (preferred — single source of truth).
2. Add a parallel `block_sound_volume(int block_type)` function alongside `block_sound_name`.
Either approach requires a change to `src/block_sound.c` and `src/game_callbacks.c:73-78`
before the ammo rows in the mapping table can be mechanically migrated.

### editor_cb_on_sound already receives volume

`src/game_callbacks.c:877-883` discards the `volume` parameter. The editor_system
already passes the correct original volumes (`src/editor_system.c:238,377,387,435,446,468,520,573,636`).
Migration for these 5 sounds is just removing the `(void)volume` and calling
`sdl2_audio_play_at_percent(ctx->audio, name, (unsigned int)volume)` instead.

### Sounds called by modern code that are in sounds/ (all present)

Every sound referenced in the modern `sdl2_audio_play` calls has a corresponding
`.wav` file in `sounds/`. No `SDL2A_ERR_NOT_FOUND` risk.

### Modern-only events

- `src/game_rules.c:160` — "bomb" at the dynamite bonus event. The original has
  no exact analog (the original doesn't have a dynamite bonus block spawner).
  Volume 50 is recommended, matching BOMB_BLK hit at `original/blocks.c:772`.
- `src/game_rules.c:200,223` — "game_over" for level-file I/O failures. Modern-only
  defensive path. Volume 99 matches `original/level.c:458`.
- `src/sfx_system.c` — `sfx_cb_on_sound` in game_callbacks.c:775-779 is registered
  but sfx_system never invokes it. No action needed.

### tone at dialogue (game_modes.c:1163)

The dialogue "tone" at volume 40 (`original/dialogue.c:301`) is distinct from
the warp-speed "tone" at volumes 10–90 (`original/main.c:750–806`). The warp-tone
is already migrated. The dialogue-tone relay at `game_modes.c:1163` is still on
`sdl2_audio_play` and needs migrating to percent 40.

---

## 6. Implementation guidance for jdc

Migrate in this order. Files with many call sites that all use one volume are
quickest; the BULLET_BLK/MAXAMMO_BLK split must be resolved first since
`game_callbacks.c` depends on it.

| File | `sdl2_audio_play` call count | Notes |
|---|---|---|
| `src/block_sound.c` | 0 (no play calls) | **Must do first**: add volume to return type or add `block_sound_volume()`; unblocks the ammo split |
| `src/game_callbacks.c` | 8 direct + block-hit relay | After block_sound fix: replace relay in `play_block_hit_sound`; fix editor_cb_on_sound to use received volume; other callbacks use per-sound fixed values |
| `src/game_rules.c` | 5 | All straightforward fixed volumes: bomb→50, game_over→99, balllost→99, applause→70 |
| `src/game_input.c` | 2 | toggle→50 (both sites) |
| `src/game_modes.c` | 5 relay sites | Relay sites: buzzer→70; presents/intro/instruct/demo/preview relays need volume stored in the sound struct (`intro_sound_t.volume` already exists in the struct; game_modes.c ignores it — use snd.volume) |

**Relay pattern for game_modes.c:** The subsystem sound structs (`intro_sound_t`,
`demo_sound_t`, `presents_sound_t`, `keys_sound_t`, `dialogue_sound_t`) already
carry a `.volume` field with the original value. The relay at each `game_modes.c`
site is currently:

```c
if (snd.name && ctx->audio)
    sdl2_audio_play(ctx->audio, snd.name);
```

Replace with:

```c
if (snd.name && ctx->audio)
    sdl2_audio_play_at_percent(ctx->audio, snd.name, (unsigned int)snd.volume);
```

This is purely mechanical — the subsystems already store the correct volumes from
the original source (verified via grep of `ctx->sound.volume` assignments).

**game_callbacks.c paddle hit:** `ball_cb_on_event` at line 363-365 handles
`BALL_EVT_PADDLE_HIT` with a direct `sdl2_audio_play(ctx->audio, "paddle")`.
Migrate to `sdl2_audio_play_at_percent(ctx->audio, "paddle", 50)`.

**game_callbacks.c ball_cb_on_sound relay:** Line 324 relays whatever name
`ball_system` emits. After migration, change to use a volume lookup. ball_system
only emits "boing" (→10) and "ball2ball" (→90) through this path. Options:
add a volume field to `ball_system_callbacks_t.on_sound`, or hard-code in the
callback with a name-to-volume switch. The switch is simplest — only two values.
