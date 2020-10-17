#!/bin/sh
set -eu
cd "$(dirname "$0")"
echo "Entering directory '$PWD'"
set -x
cc \
    -Wall -Wextra -pedantic -std=c99 \
    -fsanitize=address -Og -g \
    -I . -o example example.c minsi.c
