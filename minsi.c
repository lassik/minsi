// Copyright 2020 Lassi Kortela
// SPDX-License-Identifier: ISC

#include <sys/types.h>

#include <sys/ioctl.h>

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#ifndef MINSI_TIMEOUT_MS
#define MINSI_TIMEOUT_MS 50
#endif

#ifndef MINSI_SUBSTITUTION
#define MINSI_SUBSTITUTION "?"
#endif

#define LOBITS(n) ((1 << (n)) - 1)
#define HIBITS(n) (LOBITS(8) - LOBITS(8 - (n)))

#define HI1 HIBITS(1)
#define HI2 HIBITS(2)
#define HI3 HIBITS(3)
#define HI4 HIBITS(4)
#define HI5 HIBITS(5)

struct minsi {
    struct pollfd pollfd;
    struct termios rawMode;
    struct termios origMode;
    volatile int resizeFlag;
    char rBytes[16];
    char wBytes[4096];
    size_t wFill;
};

struct minsi *minsiFromFd(int fd)
{
    struct minsi *minsi;

    if (!isatty(fd)) {
        return 0;
    }
    minsi = calloc(1, sizeof(*minsi));
    minsi->pollfd.fd = fd;
    minsi->pollfd.events = POLLIN;
    cfmakeraw(&minsi->rawMode);
    if (tcgetattr(minsi->pollfd.fd, &minsi->origMode) == -1) {
        free(minsi);
        return 0;
    }
    return minsi;
}

struct minsi *minsiFromStdin(void) { return minsiFromFd(0); }

struct minsi *minsiFromStdout(void) { return minsiFromFd(1); }

static int minsiReadByte(struct minsi *minsi)
{
    char rBytes[1];

    if (read(minsi->pollfd.fd, rBytes, 1) != 1) {
        return -1;
    }
    return (int)(unsigned char)rBytes[0];
}

static int minsiReadByteWithTimeout(struct minsi *minsi)
{
    if (poll(&minsi->pollfd, 1, MINSI_TIMEOUT_MS) < 1) {
        return -1;
    }
    return minsiReadByte(minsi);
}

int minsiSwitchToRawMode(struct minsi *minsi)
{
    if (tcsetattr(minsi->pollfd.fd, TCSAFLUSH, &minsi->rawMode) == -1) {
        return -1;
    }
    return 0;
}

int minsiSwitchToOrigMode(struct minsi *minsi)
{
    if (tcsetattr(minsi->pollfd.fd, TCSAFLUSH, &minsi->origMode) == -1) {
        return -1;
    }
    return 0;
}

int minsiGetSize(struct minsi *minsi, int *out_x, int *out_y)
{
    struct winsize ws;

    *out_x = *out_y = 0;
    if (ioctl(minsi->pollfd.fd, TIOCGWINSZ, &ws) == -1) {
        return -1;
    }
    *out_x = ws.ws_col;
    *out_y = ws.ws_row;
    return 0;
}

static void minsiDiscardInput(struct minsi *minsi)
{
    memset(minsi->rBytes, 0, sizeof(minsi->rBytes));
}

static void minsiReadEscape(struct minsi *minsi)
{
    size_t len;
    int byt;

    len = 0;
    minsi->rBytes[len++] = 'e';
    byt = minsiReadByteWithTimeout(minsi);
    if (byt == -1) {
        return;
    }
    if ((byt != 'O') && (byt != '[')) {
        minsiDiscardInput(minsi);
        return;
    }
    minsi->rBytes[len++] = byt;
    do {
        byt = minsiReadByteWithTimeout(minsi);
        if ((byt == -1) || (len + 1 == sizeof(minsi->rBytes))) {
            minsiDiscardInput(minsi);
            return;
        }
        if (byt == ':') {
            byt = ';';
        }
        minsi->rBytes[len++] = byt;
    } while ((byt == ';') || ((byt >= '0') && (byt <= '9')));
}

static void minsiReadUtf8Rune(struct minsi *minsi, int byt)
{
    size_t len, cont;

    len = 0;
    minsi->rBytes[len++] = 'c';
    minsi->rBytes[len++] = byt;
    if (byt < 0) {
        goto fail;
    } else if (byt < HI1) {
        cont = 0;
    } else if (byt < HI2) {
        goto fail;
    } else if (byt < HI3) {
        cont = 1;
    } else if (byt < HI4) {
        cont = 2;
    } else if (byt < HI5) {
        cont = 3;
    } else {
        goto fail;
    }
    for (; cont > 0; cont--) {
        byt = minsiReadByteWithTimeout(minsi);
        if (byt < HI1) {
            goto fail;
        } else if (byt < HI2) {
            minsi->rBytes[len++] = byt;
        } else {
            goto fail;
        }
    }
    return;
fail:
    minsiDiscardInput(minsi);
}

static void minsiReadBytes(struct minsi *minsi)
{
    int byt;

    byt = minsiReadByte(minsi);
    if (byt < 0x1b) {
        minsi->rBytes[0] = '^';
        minsi->rBytes[1] = '@' + byt;
    } else if (byt == 0x1b) {
        minsiReadEscape(minsi);
    } else if (byt < 0x20) {
        minsi->rBytes[0] = '^';
        minsi->rBytes[1] = '@' + byt;
    } else if (byt < 0x7f) {
        minsiReadUtf8Rune(minsi, byt);
    } else if (byt == 0x7f) {
        minsi->rBytes[0] = '^';
        minsi->rBytes[1] = '?';
    } else {
        minsiReadUtf8Rune(minsi, byt);
    }
}

const char *minsiReadEvent(struct minsi *minsi)
{
    minsiDiscardInput(minsi);
    if (minsi->resizeFlag) {
        minsi->resizeFlag = 0;
        minsi->rBytes[0] = 'r';
    } else {
        minsiReadBytes(minsi);
    }
    return minsi->rBytes;
}

int minsiWriteFlush(struct minsi *minsi)
{
    int rv;

    rv = 0;
    if (minsi->wFill) {
        if (write(minsi->pollfd.fd, minsi->wBytes, minsi->wFill) == -1) {
            rv = -1;
        }
    }
    memset(minsi->wBytes, 0, sizeof(minsi->wBytes));
    minsi->wFill = 0;
    return rv;
}

static int minsiIsOrdinaryChar(int ch)
{
    if (ch < 0x20) {
        return 0;
    } else if (ch < 0x7f) {
        return 1;
    } else if (ch == 0x7f) {
        return 0;
    } else {
        return 1;
    }
}

static int minsiWriteRawStringN(struct minsi *minsi, const char *s, size_t n)
{
    size_t room, part;

    while (n) {
        room = sizeof(minsi->wBytes) - minsi->wFill;
        part = n;
        if (part > room) {
            part = room;
        }
        memcpy(&minsi->wBytes[minsi->wFill], s, part);
        minsi->wFill += part;
        if (minsi->wFill == sizeof(minsi->wBytes)) {
            if (minsiWriteFlush(minsi) == -1) {
                return -1;
            }
        }
        s += part;
        n -= part;
    }
    return 0;
}

static int minsiWriteRawString(struct minsi *minsi, const char *s)
{
    return minsiWriteRawStringN(minsi, s, strlen(s));
}

void minsiWriteString(struct minsi *minsi, const char *string)
{
    const char *a;
    const char *b;
    for (a = string; *a; a = b) {
        b = a;
        while (*b && minsiIsOrdinaryChar((int)(unsigned char)*b))
            b++;
        minsiWriteRawStringN(minsi, a, b - a);
        while (*b && !minsiIsOrdinaryChar((int)(unsigned char)*b)) {
            minsiWriteRawString(minsi, MINSI_SUBSTITUTION);
            b++;
        }
    }
}

void minsiWriteEscape(struct minsi *minsi, const char *string)
{
    minsiWriteRawString(minsi, "\x1b");
    minsiWriteString(minsi, string);
}

void minsiSetResizeFlag(struct minsi *minsi) { minsi->resizeFlag = 1; }
