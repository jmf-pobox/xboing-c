/*
 * XBoing - An X11 blockout style computer game
 * Simple ALSA audio implementation using aplay
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#include "include/error.h"
#include "include/audio.h"

static int audioEnabled = 0;

int SetUpAudioSystem(Display *display)
{
    /* Check if sox play is available */
    if (system("which play > /dev/null 2>&1") == 0) {
        audioEnabled = 1;
        return True;
    }
    WarningMessage("play (sox) not found - audio disabled");
    return False;
}

void FreeAudioSystem(void)
{
    audioEnabled = 0;
}

void playSoundFile(char *filename, int volume)
{
    char command[512];
    char *soundDir;
    pid_t pid;

    if (!audioEnabled) return;

    soundDir = getenv("XBOING_SOUND_DIR");
    if (soundDir != NULL)
        snprintf(command, sizeof(command),
            "play -q '%s/%s.au' 2>/dev/null &", soundDir, filename);
    else
        snprintf(command, sizeof(command),
            "play -q '%s/%s.au' 2>/dev/null &", SOUNDS_DIR, filename);

    /* Fork to avoid blocking */
    pid = fork();
    if (pid == 0) {
        /* Child process */
        execl("/bin/sh", "sh", "-c", command, NULL);
        _exit(1);
    }
    /* Parent continues immediately */

    /* Reap any finished child processes to avoid zombies */
    waitpid(-1, NULL, WNOHANG);
}

void audioDeviceEvents(void)
{
    /* Reap zombie processes */
    waitpid(-1, NULL, WNOHANG);
}

void SetMaximumVolume(int Volume)
{
    /* Volume control not implemented */
}

int GetMaximumVolume(void)
{
    return 100;
}
