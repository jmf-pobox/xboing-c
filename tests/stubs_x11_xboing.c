/*
 * stubs_x11_xboing.c — Link-time stubs for level parsing characterization tests.
 *
 * Allows test_level_parse to compile and link file.c + blocks.c + level.c +
 * error.c without a live X11 display. Only ReadNextLevel(NULL, 0, path, False)
 * is exercised at runtime. All other functions are dead code but must resolve
 * at link time.
 *
 * Stub contract for the live path (draw=False):
 *   XPolygonRegion  — returns (Region)0x1 sentinel, never dereferenced
 *   XDestroyRegion  — no-op (accepts sentinel)
 *   XClearWindow    — no-op (called by SetLevelTimeBonus unconditionally)
 *   DrawText        — no-op (called by DrawLevelTimeBonus)
 *
 * NOTE: Do NOT stub functions that are defined in the production .c files
 * we compile (file.c, blocks.c, level.c, error.c). Those modules provide
 * their own definitions. Only stub external symbols they reference.
 */

#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "ball.h"
#include "blocks.h"
#include "bonus.h"
#include "level.h"
#include "score.h"
#include "stage.h"
#include "init.h"
#include "main.h"
#include "paddle.h"
#include "special.h"

/* =========================================================================
 * X11 stubs
 * ========================================================================= */

Region XPolygonRegion(XPoint *points, int n, int fill_rule)
{
    (void)points; (void)n; (void)fill_rule;
    return (Region)0x1;
}

int XDestroyRegion(Region r)
{
    (void)r;
    return 0;
}

int XClearWindow(Display *display, Window w)
{
    (void)display; (void)w;
    return 0;
}

int XClearArea(Display *d, Window w, int x, int y,
               unsigned int width, unsigned int height, int exposures)
{
    (void)d; (void)w; (void)x; (void)y;
    (void)width; (void)height; (void)exposures;
    return 0;
}

int XTextWidth(XFontStruct *font, const char *string, int count)
{
    (void)font; (void)string; (void)count;
    return 0;
}

int XFreePixmap(Display *d, Pixmap p)
{
    (void)d; (void)p;
    return 0;
}

int XpmCreatePixmapFromData(Display *d, Drawable w, char **data,
                            Pixmap *pixmap_return, Pixmap *mask_return,
                            void *attributes)
{
    (void)d; (void)w; (void)data; (void)attributes;
    if (pixmap_return) *pixmap_return = (Pixmap)1;
    if (mask_return) *mask_return = (Pixmap)1;
    return 0; /* XpmSuccess */
}

void XpmFreeAttributes(void *attr)
{
    (void)attr;
}

int XSetForeground(Display *d, GC gc_val, unsigned long foreground)
{
    (void)d; (void)gc_val; (void)foreground;
    return 0;
}

int XFillRectangle(Display *d, Drawable w, GC gc_val, int x, int y,
                   unsigned int width, unsigned int height)
{
    (void)d; (void)w; (void)gc_val; (void)x; (void)y;
    (void)width; (void)height;
    return 0;
}

int XDrawString(Display *d, Drawable w, GC gc_val, int x, int y,
                const char *string, int length)
{
    (void)d; (void)w; (void)gc_val; (void)x; (void)y;
    (void)string; (void)length;
    return 0;
}

int XFlush(Display *d)
{
    (void)d;
    return 0;
}

/* =========================================================================
 * misc.c stubs
 * ========================================================================= */

void DrawText(Display *display, Window window, int x, int y,
              XFontStruct *font, int colour, char *text, int numChar)
{
    (void)display; (void)window; (void)x; (void)y;
    (void)font; (void)colour; (void)text; (void)numChar;
}

void RenderShape(Display *display, Window window, Pixmap pixmap,
                 Pixmap mask, int x, int y, int w, int h, int clear)
{
    (void)display; (void)window; (void)pixmap; (void)mask;
    (void)x; (void)y; (void)w; (void)h; (void)clear;
}

static char stub_homedir[] = "/tmp";
char *GetHomeDir(void) { return stub_homedir; }

/* =========================================================================
 * audio.c stubs
 * ========================================================================= */

void playSoundFile(char *name, int volume)
{
    (void)name; (void)volume;
}

void setNewRecordSoundFile(char *s)
{
    (void)s;
}

/* =========================================================================
 * score.c stubs
 * ========================================================================= */

u_long score = 0;

void AddToScore(u_long inc) { score += inc; }
void SetTheScore(u_long s) { score = s; }
void DisplayScore(Display *d, Window w, u_long s) { (void)d; (void)w; (void)s; }
u_long ComputeScore(u_long inc) { return inc; }
void DrawOutNumber(Display *d, Window w, u_long n, int x, int y)
{
    (void)d; (void)w; (void)n; (void)x; (void)y;
}

/* =========================================================================
 * sfx.c stubs
 * ========================================================================= */

void SetSfxEndFrame(int f) { (void)f; }
void changeSfxMode(int m) { (void)m; }

/* =========================================================================
 * mess.c stubs
 * ========================================================================= */

void SetCurrentMessage(Display *d, Window w, char *msg, int important)
{
    (void)d; (void)w; (void)msg; (void)important;
}

/* =========================================================================
 * special.c stubs and globals
 * ========================================================================= */

int saving = 0;
int stickyBat = 0;
int fastGun = 0;
int noWalls = 0;
int Killer = 0;
int x2Bonus = 0;
int x4Bonus = 0;

void DrawSpecials(Display *d) { (void)d; }
void Togglex2Bonus(Display *d, int on) { (void)d; (void)on; }
void Togglex4Bonus(Display *d, int on) { (void)d; (void)on; }
void ToggleKiller(Display *d, int on) { (void)d; (void)on; }
void ToggleSaving(Display *d, int on) { (void)d; (void)on; }
void ToggleFastGun(Display *d, int on) { (void)d; (void)on; }
void ToggleWallsOn(Display *d, int on) { (void)d; (void)on; }
void ToggleReverse(Display *d) { (void)d; }
void ToggleStickyBat(Display *d, int on) { (void)d; (void)on; }
void TurnSpecialsOff(Display *d) { (void)d; }

/* =========================================================================
 * gun.c stubs
 * ========================================================================= */

void SetNumberBullets(int n) { (void)n; }
void IncNumberBullets(void) {}
void DecNumberBullets(void) {}
int GetNumberBullets(void) { return 4; }
void SetUnlimitedBullets(int on) { (void)on; }
void ClearBullets(void) {}
void DrawTheBullet(Display *d, Window w, int x, int y)
{
    (void)d; (void)w; (void)x; (void)y;
}
void EraseTheBullet(Display *d, Window w, int x, int y)
{
    (void)d; (void)w; (void)x; (void)y;
}

/* =========================================================================
 * bonus.c stubs
 * ========================================================================= */

static int stub_number_bonus = 0;
enum BonusStates BonusState = 0;

void ResetNumberBonus(void) { stub_number_bonus = 0; }
int GetNumberBonus(void) { return stub_number_bonus; }
void IncNumberBonus(void) { stub_number_bonus++; }
void SetupBonusScreen(Display *d, Window w) { (void)d; (void)w; }

/* =========================================================================
 * ball.c stubs
 * ========================================================================= */

BALL balls[MAX_BALLS];
int speedLevel = 5;
int paddleDx = 0;

void SplitBallInTwo(Display *d, Window w) { (void)d; (void)w; }
void ClearBallNow(Display *d, Window w, int i) { (void)d; (void)w; (void)i; }
void ChangeBallMode(enum BallStates m, int i) { (void)m; (void)i; }
void ResetBallStart(Display *d, Window w) { (void)d; (void)w; }
void ClearAllBalls(void) {}
int GetAnActiveBall(void) { return 0; }

/* =========================================================================
 * paddle.c stubs
 * ========================================================================= */

int currentPaddleSize = 50;
int paddlePos = 247;
int reverseOn = 0;
int stickyOn = 0;

int GetPaddleSize(void) { return currentPaddleSize; }
void ChangePaddleSize(Display *d, Window w, int t) { (void)d; (void)w; (void)t; }
int paddleIsMoving(void) { return 0; }
void handlePaddleMoving(Display *d) { (void)d; }
void ResetPaddleStart(Display *d, Window w) { (void)d; (void)w; }
void SetReverseOff(void) {}

/* =========================================================================
 * eyedude.c stubs
 * ========================================================================= */

int getEyeDudeMode(void) { return 0; }
void ChangeEyeDudeMode(int m) { (void)m; }
int CheckBallEyeDudeCollision(Display *d, Window w, int i)
{
    (void)d; (void)w; (void)i;
    return 0;
}

/* =========================================================================
 * highscore.c stubs
 * ========================================================================= */

int GetHighScoreCount(void) { return 0; }
char *GetHighScoreName(int i) { (void)i; return ""; }
int GetHighScoreRanking(u_long s) { (void)s; return -1; }
int CheckAndAddScoreToHighScore(u_long s, u_long l, time_t t, char *n)
{
    (void)s; (void)l; (void)t; (void)n;
    return 0;
}
void ResetHighScore(int type) { (void)type; }

/* =========================================================================
 * stage.c stubs
 * ========================================================================= */

void DrawStageBackground(Display *d, Window w, int stageType, int clear)
{
    (void)d; (void)w; (void)stageType; (void)clear;
}

/* =========================================================================
 * init.c stubs
 * ========================================================================= */

void ShutDown(Display *d, int exitCode, char *message)
{
    (void)d; (void)exitCode; (void)message;
}

/* =========================================================================
 * main.c stubs
 * ========================================================================= */

void SetTiltsZero(void) {}

/* =========================================================================
 * dialogue.c stubs
 * ========================================================================= */

char *UserInputDialogueMessage(Display *d, char *message, int type,
                               char *entryMessage)
{
    (void)d; (void)message; (void)type; (void)entryMessage;
    return "";
}

/* =========================================================================
 * intro.c stubs
 * ========================================================================= */

void ResetIntroduction(void) {}

/* =========================================================================
 * stage.c / init.c / main.c globals
 * ========================================================================= */

Window mainWindow = (Window)0;
Window scoreWindow = (Window)0;
Window levelWindow = (Window)0;
Window playWindow = (Window)0;
Window bufferWindow = (Window)0;
Window messWindow = (Window)0;
Window specialWindow = (Window)0;
Window timeWindow = (Window)0;
Window inputWindow = (Window)0;
Window blockWindow = (Window)0;
Window typeWindow = (Window)0;

GC gccopy = (GC)0;
GC gc = (GC)0;
GC gcxor = (GC)0;
GC gcand = (GC)0;
GC gcor = (GC)0;
GC gcsfx = (GC)0;

XFontStruct *titleFont = NULL;
XFontStruct *copyFont = NULL;
XFontStruct *textFont = NULL;
XFontStruct *dataFont = NULL;

int red = 0, tann = 0, yellow = 0, green = 0;
int white = 0, black = 0, blue = 0, purple = 0;
int greens[7] = {0};
int reds[7] = {0};

Colormap colormap = (Colormap)0;

int noSound = 1;
int debug = 0;
int noicon = 0;

int frame = 0;
int mode = 0;
int oldMode = 0;
int modeSfx = 0;
int gameActive = 0;
time_t pausedTime = 0;
