# Makefile for XBoing - Modern Linux version
# Original: Justin C. Kibell, 1993-1997
# Modernized for current Linux systems

CC = gcc
CFLAGS = -O2 -Wall -Wno-unused-result -Wno-format-overflow -Wno-format-truncation \
         -I./include -I/usr/include/X11 \
         -DHIGH_SCORE_FILE=\"./.xboing.scr\" \
         -DLEVEL_INSTALL_DIR=\"./levels\" \
         -DSOUNDS_DIR=\"./sounds\" \
         -DREADMEP_FILE=\"./docs/problems.doc\" \
         -DAUDIO_AVAILABLE=\"True\" \
         -DAUDIO_FILE=\"audio/LINUXaudio.c\" \
         -DNeedFunctionPrototypes=1

LDFLAGS =
LIBS = -lXpm -lX11 -lm

SRCS = version.c main.c score.c error.c ball.c blocks.c init.c \
       stage.c level.c paddle.c mess.c intro.c bonus.c sfx.c \
       highscore.c misc.c inst.c gun.c keys.c audio.c special.c \
       presents.c demo.c file.c preview.c dialogue.c eyedude.c \
       editor.c keysedit.c

OBJS = $(SRCS:.c=.o)

all: audio.c version.c xboing

xboing: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

version.c:
	sh ./version.sh xboing

audio.c: audio/LINUXaudio.c
	rm -f $@
	ln -s $< $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) xboing version.c audio.c

.PHONY: all clean
