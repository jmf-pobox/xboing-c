# Dialogue System — Original Source Research

Date: 2026-05-26

## Dimensions and Position

- `DIALOGUE_WIDTH` = `(int)(PLAY_WIDTH / 1.3)` = 380px
- `DIALOGUE_HEIGHT` = 120px
- Border: 4px red (XCreateSimpleWindow border)
- Position in mainWindow: x=92, y=295 (centered on combined play+main area)
- Parent: mainWindow (not playWindow)

## Visual Layout (coordinates relative to inputWindow interior)

| Element | X | Y | Size | Color |
|---------|---|---|------|-------|
| Background | 0 | 0 | 380x120 | Stone arch tile (bgrnd.xpm, 32x32, 3 greys) |
| Border | — | — | 4px all sides | Red |
| Icon (text.xpm) | 2 | 4 | 32x32 | — |
| Message (shadow) | centered+2 | 12 | — | Black |
| Message (main) | centered | 10 | — | Green |
| Separator line | 10 | 45 | 370x2 | White |
| Input text (shadow) | centered+2 | 72 | — | Black |
| Input text (main) | centered | 70 | — | Yellow |
| Question icon (if empty) | 174 | 70 | 25x30 | question.xpm |

## Font

Adobe Helvetica, medium, roman, 18pt. Shadow rendered at (+2, +2) in black.

## Close Animation (WindowFadeEffect)

Progressive black grid overlay: draws vertical+horizontal lines at 12px intervals,
incrementing offset 0→12 over 13 frames. Effect: window content replaced by dense
black grid until fully black, then unmapped. Skipped if SFX disabled.

## Validation Modes

| Mode | Constant | Accepted | Max Length |
|------|----------|----------|------------|
| TEXT_ENTRY_ONLY | 1 | space through 'z' | Width-bound (360px) |
| NUMERIC_ENTRY_ONLY | 2 | '0' through '9' | Width-bound |
| ALL_ENTRY | 3 | space through '~' | Width-bound |
| YES_NO_ENTRY | 4 | y/Y/n/N only | 1 character |

## Key Handling

- Return or Escape: closes dialogue (DIALOGUE_UNMAP)
- Backspace/Delete: removes last char, plays "key" sound
- Valid char: appends, plays "click" sound (vol 70)
- Width overflow: plays "tone" sound (vol 40), rejected

## All Callers

| Location | Message | Validation | Trigger |
|----------|---------|------------|---------|
| main.c:866 | "Exit XBoing you wimp? [y/n]" | YES_NO | Q key |
| main.c:507 | "Abort current game? [y/n]" | YES_NO | Escape in game |
| editor.c:1039 | "Unsaved work, exit editor? [y/n]" | YES_NO | Q in editor (modified) |
| editor.c:1042 | "Exit the level editor? [y/n]" | YES_NO | Q in editor (clean) |
| editor.c:1056 | "Unsaved work, continue load? [y/n]" | YES_NO | L in editor (modified) |
| editor.c:1086 | "Clear this level? [y/n]" | YES_NO | C in editor |
| editor.c:845 | "Input load level number please." | NUMERIC | L in editor (clean) |
| editor.c:900 | "Input save level number please." | NUMERIC | S in editor |
| editor.c:942 | "Input game time in seconds." | NUMERIC | T in editor |
| editor.c:974 | "Input new name for level please." | TEXT | N in editor |
| level.c:261 | "Input game starting level number." | NUMERIC | W key |
| level.c:440 | "Words of wisdom Boing Master?" | TEXT | New #1 highscore |

All callers use TEXT_ICON. DISK_ICON is dead code.

## Assets

- `bitmaps/bgrnds/bgrnd.xpm` — 32x32 stone arch tile (BACKGROUND_1)
- `bitmaps/text.xpm` — 32x32 document icon (TEXT_ICON)
- `bitmaps/question.xpm` — 25x30 yellow question mark (empty input placeholder)
- `bitmaps/floppy.xpm` — 32x32 red floppy disk (DISK_ICON, unused)
