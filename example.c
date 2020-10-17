// Copyright 2020 Lassi Kortela
// SPDX-License-Identifier: ISC

#include <signal.h>
#include <stdio.h>
#include <string.h>

#include <minsi.h>

static struct minsi *minsi;

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

static void clear(void)
{
    minsiWriteEscape(minsi, "[2J");
    minsiWriteEscape(minsi, "[H");
}

static void update(const char *part1, const char *part2)
{
    clear();
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
    int shouldquit;

    if (!(minsi = minsiFromStdin())) {
        fprintf(stderr, "Cannot open terminal\n");
        return 1;
    }
    initSignalHandler();
    minsiSwitchToRawMode(minsi);
    update("Press some keys. Press 'q' to quit.", 0);
    minsiWriteFlush(minsi);
    shouldquit = 0;
    while (!shouldquit) {
        static char buf[64];
        const char *event = minsiReadEvent(minsi);
        memset(buf, 0, sizeof(buf));
        switch (event[0]) {
        case '^':
            update("Control character: ", &event[1]);
            break;
        case 'c':
            update("Character: ", &event[1]);
            if (!strcmp(&event[1], "q")) {
                shouldquit = 1;
            }
            break;
        case 'e':
            update("Escape sequence: ", &event[1]);
            break;
        case 'r':
            update("Window resize", 0);
            break;
        default:
            update("Unknown event", 0);
            break;
        }
    }
    update(0, 0);
    minsiSwitchToOrigMode(minsi);
    return 0;
}
