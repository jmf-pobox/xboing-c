# Sound-Effect Parity Audit — Original vs. Modern Port

**Date:** 2026-07-14
**Auditor:** jck (Justin C. Kibell), mission `m-2026-07-14-007`
**Evaluator:** sjl

## Scope

This audit covers *audio* sound-effect parity only: does the modern
SDL2 port (`src/`) play the same sound sample, on the same trigger,
at the same volume, as the 1996 original (`original/`)? It does not
cover audio backend quality, mixing architecture, or code structure.
`original/sfx.c` is excluded — it implements *visual* effects (window
shake, border glow, fade) and contains no `playSoundFile` calls.

## Method

1. Grepped every `playSoundFile(name, volume)` call site across
   `original/*.c` (excludes the platform backend shims in
   `original/audio/*.c`, which implement the function, not call it).
2. Grepped every `sdl2_audio_play_at_percent` / `on_sound` /
   `fire_sound` / `play_sound` call site across `src/*.c`.
3. Read the surrounding code at each site in both trees to identify
   the triggering game event (not just the literal sample name).
4. Cross-referenced row by row: same event → same sample → same
   volume is a MATCH; any divergence is classified MISSING / EXTRA /
   WRONG-SOUND / WRONG-TRIGGER / VOLUME-MISMATCH.
5. Checked `docs/DESIGN.md` for an ADR on every divergence found.

The original's `playSoundFile` call sites (84 raw calls, ~75 distinct
event rows after collapsing repeated literal calls such as the 9-tone
bonus ladder) were matched against the modern tree. 3 genuine
deviations were found; the remainder are
faithful 1:1 ports, including one place where the original itself is
silent by omission (unmapped `DYNAMITE_BLK` in `PlaySoundForBlock`)
and the modern port correctly reproduces that silence rather than
inventing a sound.

## Event → Sound Comparison Table

### Block hits (`PlaySoundForBlock` / `block_sound_lookup`)

| Event | Original | Modern | Verdict |
|-------|----------|--------|---------|
| BOMB_BLK hit | `bomb@50` — `original/blocks.c:772` | `bomb@50` — `src/block_sound.c:12` | MATCH |
| BULLET_BLK hit | `ammo@30` — `blocks.c:776` | `ammo@30` — `block_sound.c:14` | MATCH |
| MAXAMMO_BLK hit | `ammo@70` — `blocks.c:780` | `ammo@70` — `block_sound.c:16` | MATCH |
| RED/GREEN/BLUE/TAN/PURPLE/YELLOW/COUNTER/RANDOM/DROP hit | `touch@99` — `blocks.c:792` | `touch@99` — `block_sound.c:26` | MATCH |
| ROAMER_BLK hit | `ouch@99` — `blocks.c:796` | `ouch@99` — `block_sound.c:28` | MATCH |
| EXTRABALL_BLK hit | `ddloo@99` — `blocks.c:800` | `ddloo@99` — `block_sound.c:30` | MATCH |
| MGUN_BLK hit | `mgun@99` — `blocks.c:804` | `mgun@99` — `block_sound.c:32` | MATCH |
| WALLOFF_BLK hit | `wallsoff@99` — `blocks.c:808` | `wallsoff@99` — `block_sound.c:34` | MATCH |
| BONUSX2/X4/BONUS_BLK hit | `gate@99` — `blocks.c:814` | `gate@99` — `block_sound.c:38` | MATCH |
| REVERSE_BLK hit | `warp@99` — `blocks.c:818` | `warp@99` — `block_sound.c:40` | MATCH |
| PAD_SHRINK_BLK hit | `wzzz2@99` — `blocks.c:822` | `wzzz2@99` — `block_sound.c:42` | MATCH |
| PAD_EXPAND_BLK hit | `wzzz@99` — `blocks.c:826` | `wzzz@99` — `block_sound.c:44` | MATCH |
| MULTIBALL_BLK hit | `spring@80` — `blocks.c:830` | `spring@80` — `block_sound.c:46` | MATCH |
| TIMER_BLK hit | `bonus@50` — `blocks.c:834` | `bonus@50` — `block_sound.c:48` | MATCH |
| STICKY_BLK hit | `sticky@90` — `blocks.c:838` | `sticky@90` — `block_sound.c:50` | MATCH |
| DEATH_BLK hit | `evillaugh@99` — `blocks.c:842` | `evillaugh@99` — `block_sound.c:52` | MATCH |
| BLACK_BLK hit | `metal@99` — `blocks.c:846` | `metal@99` — `block_sound.c:54` | MATCH |
| HYPERSPACE_BLK teleport | `hypspc@99` — `ball.c:872` (dedicated call, not the generic dispatch) | `hypspc@99` — `game_callbacks.c:210` (dedicated call, matches `block_system.c:536-537` immune-to-explosion special-case) | MATCH |
| DYNAMITE_BLK "hit" | Unreachable / silent — `blocks.c` has no case for it in `PlaySoundForBlock`; falls to `default: ErrorMessage(...)` (`blocks.c:857-859`). This is an original bug: `DYNAMITE_BLK` is never itself a hittable type (see the dynamite-spawn analysis below), so the case is dead code. | Silent — `block_sound.c:57-62` explicitly returns `{NULL, 0}` with a comment citing the original gap | MATCH (bug-for-bug, deliberately documented) |

### Ball physics

| Event | Original | Modern | Verdict |
|-------|----------|--------|---------|
| Left/right/top wall bounce | `boing@10` — `ball.c:1049,1067,1085` | `boing@10` — `ball_system.c:490,510,530` | MATCH |
| Paddle bounce | `paddle@50` — `ball.c:1096` | `paddle@50` — `game_callbacks.c:368` | MATCH |
| Ball-to-ball collision | `ball2ball@90` — `ball.c:1335` | `ball2ball@90` — `ball_system.c:756` | MATCH |
| Eyedude appears/walks on | `hithere@100` — `eyedude.c:280` | `hithere@100` — `eyedude_system.c:187` | MATCH |
| Eyedude killed by ball | `supbons@80` — `eyedude.c:383` | `supbons@80` — `eyedude_system.c:260` | MATCH |
| Ball launch (space to release from paddle) | No sound anywhere in `ball.c` / `main.c` | No sound in `src/` | MATCH (both silent) |

### Gun / ammo

| Event | Original | Modern | Verdict |
|-------|----------|--------|---------|
| Single shot fired | `shoot@80` — `gun.c:215` | `shoot@80` — `gun_system.c:92` | MATCH |
| Ball-shot (bullet hits a ball) | `ballshot@50` — `gun.c:288` | `ballshot@50` — `gun_system.c:182` | MATCH |
| Machine-gun burst | `shotgun@50` — `gun.c:504` | `shotgun@50` — `gun_system.c:337` | MATCH |
| Fire with no ammo | `click@99` — `gun.c:511` | `click@99` — `gun_system.c:347` | MATCH |

### Dialogue box

| Event | Original | Modern | Verdict |
|-------|----------|--------|---------|
| Character typed | `click@70` — `dialogue.c:295` | `click@70` — `dialogue_system.c:190` | MATCH |
| Input overflow (field full / width exceeded) | `tone@40` — `dialogue.c:301` | `tone@40` — `dialogue_system.c:176` | MATCH |
| Backspace | `key@70` — `dialogue.c:338` | `key@70` — `dialogue_system.c:162` | MATCH |

### Level / game rules

| Event | Original | Modern | Verdict |
|-------|----------|--------|---------|
| Level timer reaches 0 (countdown warning) | `buzzer@70` — `level.c:143-148` (`DecLevelTimeBonus`) | **No call anywhere in `src/`** | **MISSING** |
| Level complete (all required blocks cleared) | `applause@70` — `level.c:413` | `applause@70` — `game_rules.c:373` | MATCH |
| New rank-1 in the GLOBAL high-score table ("boing master") | `youagod@99` — `level.c:437`, ranking always read from GLOBAL table (`highscore.c:622-628`) | `youagod@99` — `game_modes.c:1080`, explicitly gated on the GLOBAL table (`hs_global`) | MATCH |
| Game over (lives exhausted) | `game_over@99` — `level.c:458` | `game_over@99` — `game_rules.c:314` | MATCH |
| Ball lost, lives remain (respawn) | `balllost@99` — `level.c:477` | `balllost@99` — `game_rules.c:325` | MATCH |
| Level file missing/corrupt, forces game end | Original: `ShutDown()` → `exit()`, no sound, process terminates (`file.c` `SetupStage`) | `game_over@99` — `game_rules.c:200,223`, then routes to HIGHSCORE instead of exiting. Documented deviation: **ADR-044** in `docs/DESIGN.md` | EXTRA, but **documented and approved** |
| New game start (space from INTRO/HIGHSCORE/etc.) | Silent — `main.c:539-552` (`handleIntroKeys`, `XK_space` case has no `playSoundFile`) | `buzzer@70` — `game_modes.c:188` (`start_new_game`) | **EXTRA / WRONG-TRIGGER** (undocumented — no ADR) |
| `Q` key, confirmed quit ("Exit XBoing you wimp?" → yes) | `game_over@100` — `main.c:714-715` (`handleExitKeys`), then `exit()` | Silent — confirmed quit pushes `SDL_QUIT` (`game_modes.c:1295-1306`) which `game_main.c:99-101` handles with no audio call | **MISSING** (also a volume divergence: original used 100%, distinct from the 99% "game over" used elsewhere) |
| Random bonus-block spawn rolls "dynamite" (1 of 27 possible spawns, `case 25`) | Silent at spawn — `main.c:1072-1103` calls `SetExplodeAllType()` (`blocks.c:1001-1054`), which only flags one block `explodeAll=True` and draws an overlay icon; it has no `playSoundFile` call. The `bomb@50` sample is reserved exclusively for a distinct, level-authored `BOMB_BLK` tile type being hit (`blocks.c:771-772`) | `bomb@50` — `game_rules.c:160` (`try_spawn_bonus`, the `roll == 25` branch) | **EXTRA / WRONG-TRIGGER** (undocumented — no ADR; also misappropriates a sample reserved for an unrelated block type) |
| Extra life awarded (100,000-pt threshold) | Silent — `level.c:359-368` (`AddExtraLife`) only sets a status message | Silent — `score_system.c` / `game_callbacks.c:187` only increments `lives_left` | MATCH (both silent) |
| Board tilt (manual `T` or auto-tilt) | Silent — `ball.c:490-503` (`DoBoardTilt`), `main.c:451-473` | Silent — `game_input.c:297-316` | MATCH (both silent) |
| `A` key, audio on/off toggle | Silent — `main.c:396-422` (`handleSoundKey`) | Silent — `game_input.c:197-210` | MATCH (both silent) |

### Bonus tally screen (`bonus.c` / `bonus_system.c`)

| Event | Original | Modern | Verdict |
|-------|----------|--------|---------|
| "Congratulations" text | Silent — `bonus.c:263-278` (`DoScore`) | Silent — `bonus_system.c` `do_score` | MATCH |
| Coin tally voided (timer ran out before bonus screen) | `Doh4@80` — `bonus.c:292` | `Doh4@80` — `bonus_system.c:254` | MATCH |
| Zero coins collected | `Doh1@80` — `bonus.c:315` | `Doh1@80` — `bonus_system.c:264` | MATCH |
| Super bonus (>10 coins collected) | `supbons@80` — `bonus.c:334` | `supbons@80` — `bonus_system.c:273` | MATCH |
| Per-coin tally animation tick | `bonus@50` — `bonus.c:366` | `bonus@50` — `bonus_system.c:288` | MATCH |
| No level bonus (timer ran out) | `Doh2@80` — `bonus.c:421` | `Doh2@80` — `bonus_system.c:317` | MATCH |
| No bullets to bonus | `Doh3@80` — `bonus.c:450` | `Doh3@80` — `bonus_system.c:332` | MATCH |
| Per-bullet tally animation tick | `key@50` — `bonus.c:473` | `key@50` — `bonus_system.c:346` | MATCH |
| No time bonus (timer ran out) | `Doh4@80` — `bonus.c:520` | `Doh4@80` — `bonus_system.c:375` | MATCH |
| End-text ("Prepare for level N+1") | `applause@80` — `bonus.c:600` | `applause@80` — `bonus_system.c:387` | MATCH |

### Attract-cycle screen transitions

| Event | Original | Modern | Verdict |
|-------|----------|--------|---------|
| Presents screen: flag/title enters | `intro@40` — `presents.c:234` | `intro@40` — `presents_system.c:111` | MATCH |
| Presents screen: XBOING letter stamped / "II" stamped | `stamp@90` — `presents.c:331,342` | `stamp@90` — `presents_system.c:172,189` | MATCH |
| Presents screen: sparkle/shine begins | `ping@70` — `presents.c:372` | `ping@70` — `presents_system.c:214` | MATCH |
| Presents screen: typewriter character (×3 lines) | `key@60` — `presents.c:423,458,493` | `key@60` — `presents_system.c:305` | MATCH |
| Presents screen: curtain wipe finish | `whoosh@70` — `presents.c:519` | `whoosh@70` — `presents_system.c:351` | MATCH |
| Intro/title screen finish | `whoosh@50` — `intro.c:352` | `whoosh@50` — `intro_system.c:240` | MATCH |
| Instructions screen finish | `shark@50` — `inst.c:210` | `shark@50` — `intro_system.c:235` | MATCH |
| Demo screen finish | `whizzo@50` — `demo.c:255` | `whizzo@50` — `demo_system.c:205` | MATCH |
| Preview screen: "looksbad" flavor line (1/3 chance) | `looksbad@80` — `preview.c:156` | `looksbad@80` — `demo_system.c:183` | MATCH |
| Preview screen finish | `whizzo@50` — `preview.c:168` | `whizzo@50` — `demo_system.c:205` (shared handler) | MATCH |
| Keys (game-control display) screen finish | `boing@50` — `keys.c:300` | `boing@50` — `keys_system.c:211` | MATCH |
| KeysEdit (rebinding) screen finish | `warp@50` — `keysedit.c:257` | `warp@50` — `keys_system.c:216` | MATCH |
| Highscore screen finish | `gate@50` — `highscore.c:492` | `gate@50` — `highscore_system.c:174` | MATCH |

### Level editor

| Event | Original | Modern | Verdict |
|-------|----------|--------|---------|
| Quit editor (evil laugh) | `evillaugh@50` — `editor.c:394` | `evillaugh@50` — `editor_system.c:269` | MATCH |
| Bonus/time adjust (×4 call sites) | `bonus@20` — `editor.c:441,455,531,544` | `bonus@20` — `editor_system.c:411,421,474,485` | MATCH |
| Flip board horizontal | `wzzz@50` — `editor.c:653` | `wzzz@50` — `editor_system.c:507` | MATCH |
| Scroll board horizontal | `sticky@50` — `editor.c:688` | `sticky@50` — `editor_system.c:612` | MATCH |
| Flip board vertical | `wzzz2@50` — `editor.c:734` | `wzzz2@50` — `editor_system.c:559` | MATCH |
| Scroll board vertical | `sticky@50` — `editor.c:772` | `sticky@50` — `editor_system.c:675` | MATCH |

### Global keys

| Event | Original | Modern | Verdict |
|-------|----------|--------|---------|
| `G`: toggle mouse/keyboard control | `toggle@50` — `main.c:392-393` | `toggle@50` — `game_input.c:256` | MATCH |
| `H`/`h`: show personal/global highscores | `toggle@50` — `main.c:619-620,634-635` | `toggle@50` — `game_input.c:221` | MATCH |
| `1`-`9`: set warp speed | `tone@(10..90, step 10)` — `main.c:750-806` | `tone@(s*10)` — `game_input.c:189` | MATCH |
| `I`: iconify (fullscreen toggle in modern) | Silent — `main.c:853-857` | Silent — `game_input.c:246-247` | MATCH |

## Ranked Deviation Table

| # | Severity | Deviation | Citations | Player-audible impact | ADR? |
|---|----------|-----------|-----------|------------------------|------|
| 1 | **HIGH** | `buzzer@70` fires on every new-game start (an event the original never made noise for) instead of on level-timer expiry (an event the modern port never makes noise for at all) | Original: `original/level.c:143-148`. Modern spurious trigger: `src/game_modes.c:188` (`start_new_game`). Modern missing trigger: no call in `src/game_modes.c:318-333` (the timer-countdown tick) | Every single new game now opens with an unexplained warning-buzzer chime the 1995 player never heard. Conversely, the countdown timer's actual "time is running out" audio cue — a real gameplay signal — never sounds, so players get zero audio warning before the level-bonus timer hits zero. | None found |
| 2 | **HIGH** | `bomb@50` fires whenever the periodic random bonus-spawn rolls "dynamite" (case 25 of 27); the original is silent at that moment and reserves `bomb@50` exclusively for a real `BOMB_BLK` tile being hit | Original silent path: `original/main.c:1072-1103` → `original/blocks.c:1001-1054` (`SetExplodeAllType`). Original's real trigger for `bomb@50`: `original/blocks.c:771-772`. Modern: `src/game_rules.c:160` | Roughly 1-in-27 bonus spawns (every ~15-60s of play, per `BONUS_SEED`) plays `bomb@50` where the original is silent. (Correction per sjl review: the modern dynamite case immediately clears every matching-color block in the same tick — `src/game_rules.c:145-160` → `block_system_clear` — so the sound accompanies a real mass-clear, not "nothing exploding"; the deviation is that the *original* is silent at this event and reserves `bomb@50` for a real `BOMB_BLK` tile hit.) | None found |
| 3 | **MEDIUM** | Confirmed `Q`-quit never plays `game_over@100` before exit | Original: `original/main.c:714-715` (`handleExitKeys`), reached via `original/main.c:864-868`. Modern: `src/game_modes.c:1295-1306` (pushes `SDL_QUIT`), `src/game_main.c:99-101` (handles it silently) | Audible impact is muted by the window closing almost immediately afterward, but the original always gave a distinct 100%-volume sting on confirmed quit that the modern port drops entirely — including its unique volume (100%, vs. 99% everywhere else `game_over` plays). | None found |

Everything else audited — all 17 block-type hit sounds, all paddle/wall/ball-ball physics sounds, all 4 gun sounds, all 3 dialogue sounds, all 10 bonus-tally-sequence sounds, all 13 attract-cycle screen-transition sounds, all 6 editor sounds, and all 4 global-key sounds — is a byte-for-byte trigger/volume match, including one case (`DYNAMITE_BLK`) where the modern port faithfully reproduces an original bug (a dead, unreachable case in `PlaySoundForBlock`) rather than silently "fixing" it.

## Recommended Follow-up Beads

1. **Fix `buzzer` mistrigger and missing timer-expiry cue.** Remove
   the `sdl2_audio_play_at_percent(ctx->audio, "buzzer", 70)` call
   from `start_new_game()` (`src/game_modes.c:188`) and add it to the
   level-timer countdown path (`src/game_modes.c:318-333`) at the
   moment `ctx->time_remaining` transitions from 1 to 0, matching
   `original/level.c:143-148`.
2. **Remove the spurious `bomb` sound from the dynamite bonus-spawn
   roll.** Delete the `sdl2_audio_play_at_percent(ctx->audio, "bomb",
   50)` call in `src/game_rules.c:160` (`try_spawn_bonus`, `roll ==
   25` branch). The original's `SetExplodeAllType` is silent at spawn
   time; no replacement sound is needed for parity.
3. **Restore the quit-confirmation sting.** Play `game_over@100`
   (matching the original's distinct 100% volume, `original/main.c:
   714-715`) when the `Q`-key "Exit XBoing you wimp?" dialogue is
   confirmed, before `SDL_QUIT` is pushed — likely in
   `src/game_modes.c` around the `quit_pending` handling
   (`game_modes.c:1295-1306`).

Each of these is a one-line-or-few-line, behavior-only fix with no
architectural implications — good T3/Direct-tier work with an
`implement` + `review` mission pair. None require a design mission;
the trigger points and correct sample/volume are fully specified by
the citations above.
