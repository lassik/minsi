// Copyright 2020 Lassi Kortela
// SPDX-License-Identifier: ISC

#include <signal.h>
#include <stdio.h>
#include <string.h>

#include <minsi.h>

static struct minsi *minsi;
static int width, height;

struct pair {
    const char *a;
    const char *b;
};

static const char *lookup(const struct pair *pair, const char *a)
{
    for (; pair->a; pair++) {
        if (!strcmp(pair->a, a)) {
            return pair->b;
        }
    }
    return 0;
}

static const struct pair lookupControl[] = {
    { "H", "Backspace" },
    { "I", "Tab" },
    { "J", "Return" },
    { "M", "Return" },
    { "?", "Backspace" },
    { 0, 0 },
};

static const struct pair lookupEscape[] = {
    { "", "Escape" },
    { "[15~", "F5" },
    { "[17~", "F6" },
    { "[18~", "F7" },
    { "[19~", "F8" },
    { "[1;10A", "Alt-Shift-Up" },
    { "[1;10B", "Alt-Shift-Down" },
    { "[1;10C", "Alt-Shift-Right" },
    { "[1;10D", "Alt-Shift-Left" },
    { "[1;2A", "Shift-Up" },
    { "[1;2B", "Shift-Down" },
    { "[1;2C", "Shift-Right" },
    { "[1;2D", "Shift-Left" },
    { "[1;2F", "Shift-End" },
    { "[1;2H", "Shift-Home" },
    { "[1;2P", "PrintScreen" },
    { "[1;5A", "Control-Alt-Up" },
    { "[1;5B", "Control-Alt-Down" },
    { "[1;5C", "Control-Alt-Right" },
    { "[1;5D", "Control-Alt-Left" },
    { "[1;6A", "Control-Shift-Up" },
    { "[1;6B", "Control-Shift-Down" },
    { "[1;6C", "Control-Shift-Right" },
    { "[1;6D", "Control-Shift-Left" },
    { "[20~", "F9" },
    { "[21~", "F10" },
    { "[23~", "F11" },
    { "[24~", "F12" },
    { "[3~", "Delete" },
    { "[5~", "PageUp" },
    { "[6~", "PageDown" },
    { "[A", "Up" },
    { "[B", "Down" },
    { "[C", "Right" },
    { "[D", "Left" },
    { "[e", "F19" },
    { "[F", "End" },
    { "[f", "F20" },
    { "[g", "F21" },
    { "[h", "F22" },
    { "[H", "Home" },
    { "[i", "F23" },
    { "[j", "F24" },
    { "[k", "F25" },
    { "[l", "F26" },
    { "[m", "F27" },
    { "[n", "F28" },
    { "[o", "F29" },
    { "[p", "F30" },
    { "[q", "F31" },
    { "[r", "F32" },
    { "[s", "F33" },
    { "[t", "F34" },
    { "[u", "F35" },
    { "[v", "F36" },
    { "[w", "F37" },
    { "[x", "F38" },
    { "[y", "F39" },
    { "[z", "F40" },
    { "[Z", "Shift-Tab" },
    { "[{", "F48" },
    { "OF", "End" },
    { "OH", "Home" },
    { "OP", "F1" },
    { "OQ", "F2" },
    { "OR", "F3" },
    { "OS", "F4" },
    { 0, 0 },
};

static void sigwinch(int signo)
{
    (void)signo;
    minsiSetResizeFlag(minsi);
}

static void initSignalHandler(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigwinch;
    sigaction(SIGWINCH, &sa, 0);
}

static void gotoTopLeft(void) { minsiWriteEscape(minsi, "[H"); }

static void gotoNextLine(void) { minsiWriteEscape(minsi, "[1E"); }

static void clear(void)
{
    minsiWriteEscape(minsi, "[2J");
    gotoTopLeft();
}

static void drawBox(void)
{
    const char cornerTL[] = "l";
    const char cornerTR[] = "k";
    const char cornerBL[] = "m";
    const char cornerBR[] = "j";
    const char horzLine[] = "q";
    const char vertLine[] = "x";
    int x, y;
    int r, b;

    r = width - 1;
    b = height - 1;
    minsiWriteEscape(minsi, "(0");
    minsiWriteEscape(minsi, "[0m");
    gotoTopLeft();
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            if ((y == 0) && (x == 0)) {
                minsiWriteString(minsi, cornerTL);
            } else if ((y == 0) && (x == r)) {
                minsiWriteString(minsi, cornerTR);
            } else if ((y == b) && (x == 0)) {
                minsiWriteString(minsi, cornerBL);
            } else if ((y == b) && (x == r)) {
                minsiWriteString(minsi, cornerBR);
            } else if ((y == 0) || (y == b)) {
                minsiWriteString(minsi, horzLine);
            } else if ((x == 0) || (x == r)) {
                minsiWriteString(minsi, vertLine);
            } else {
                minsiWriteString(minsi, " ");
            }
        }
        gotoNextLine();
    }
    minsiWriteEscape(minsi, "(B");
}

static void update(const char *part1, const char *part2)
{
    clear();
    drawBox();
    gotoTopLeft();
    gotoNextLine();
    gotoNextLine();
    minsiWriteEscape(minsi, "[4C");
    if (part1) {
        minsiWriteEscape(minsi, "[31m");
        minsiWriteString(minsi, part1);
    }
    if (part2) {
        minsiWriteEscape(minsi, "[32m");
        minsiWriteString(minsi, part2);
    }
    minsiWriteFlush(minsi);
}

int main(void)
{
    const char *translat;
    const char *event;
    int shouldQuit;

    if (!(minsi = minsiFromStdin())) {
        fprintf(stderr, "Cannot open terminal\n");
        return 1;
    }
    minsiSwitchToRawMode(minsi);
    initSignalHandler();
    minsiGetSize(minsi, &width, &height);
    minsiWriteEscape(minsi, "[?1000h");
    update("Press some keys. Press 'q' to quit.", 0);
    shouldQuit = 0;
    while (!shouldQuit) {
        event = minsiReadEvent(minsi);
        switch (event[0]) {
        case '^':
            translat = lookup(lookupControl, &event[1]);
            if (translat) {
                update("Special key: ", translat);
            } else {
                update("Control character: ", &event[1]);
            }
            break;
        case 'c':
            update("Character: ", &event[1]);
            if (!strcmp(&event[1], "q")) {
                shouldQuit = 1;
            }
            break;
        case 'e':
            translat = lookup(lookupEscape, &event[1]);
            if (translat) {
                update("Special key: ", translat);
            } else {
                update("Escape sequence: ", &event[1]);
            }
            break;
        case 'm':
            update("Mouse event", 0);
            break;
        case 'r':
            minsiGetSize(minsi, &width, &height);
            update("Window resize", 0);
            break;
        default:
            update("Unknown event", 0);
            break;
        }
    }
    clear();
    minsiWriteFlush(minsi);
    minsiSwitchToOrigMode(minsi);
    return 0;
}
