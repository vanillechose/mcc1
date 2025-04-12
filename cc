#!/usr/bin/env bash

CC="./cc1"
AS="gcc -fno-pie -static"

tmp="$(mktemp).S"

if ${CC} > ${tmp}; then
    ${AS} "${tmp}" "$@"
else
    exit 1
fi

rm -f ${tmp}
