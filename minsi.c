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

static void minsiClearBytes(struct minsi *minsi)
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
        // TODO
        minsiClearBytes(minsi);
        return;
    }
    minsi->rBytes[len++] = byt;
    do {
        byt = minsiReadByteWithTimeout(minsi);
        if ((byt == -1) || (len == sizeof(minsi->rBytes) - 1)) {
            minsiClearBytes(minsi);
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
    minsiClearBytes(minsi);
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
    minsiClearBytes(minsi);
    if (minsi->resizeFlag) {
        minsi->resizeFlag = 0;
        minsi->rBytes[0] = 'r';
    } else {
        minsiReadBytes(minsi);
    }
    return minsi->rBytes;
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
    if (n) {
        if (write(minsi->pollfd.fd, s, n) == -1) {
            return -1;
        }
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

void minsiWriteFlush(struct minsi *minsi) { (void)minsi; }

void minsiSetResizeFlag(struct minsi *minsi) { minsi->resizeFlag = 1; }
