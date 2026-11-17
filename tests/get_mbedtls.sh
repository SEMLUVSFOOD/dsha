#!/bin/sh
# SPDX-License-Identifier: MIT
#
# Copyright (c) 2026 DSHA contributors
#
# Downloads and builds Mbed TLS 3.6 (the version in BlocksDS) for the desktop
# tests, for systems that don't have it (Ubuntu 24.04 only has 2.28). The
# result goes to tests/build/mbedtls, where tests/Makefile finds it.

set -eu

VERSION=3.6.4
SHA256=40d83e4040c3e548a61f6ac19b697b0380149abafd6923196a70b6067b543261
# The "easy-make-lib" release has the generated sources: no Python needed
URL="https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-$VERSION/mbedtls-$VERSION-easy-make-lib.tar.bz2"

cd "$(dirname "$0")"
PREFIX="$PWD/build/mbedtls"
if [ -f "$PREFIX/lib/libmbedtls.a" ]; then
    echo "Mbed TLS is already in $PREFIX"
    exit 0
fi

WORK="$PWD/build/mbedtls-src"
rm -rf "$WORK"
mkdir -p "$WORK"
curl -sSfL -o "$WORK/mbedtls.tar.bz2" "$URL"
if command -v sha256sum >/dev/null; then
    echo "$SHA256  $WORK/mbedtls.tar.bz2" | sha256sum -c -
else
    echo "$SHA256  $WORK/mbedtls.tar.bz2" | shasum -a 256 -c -
fi
tar -xjf "$WORK/mbedtls.tar.bz2" -C "$WORK"

JOBS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)
make -C "$WORK/mbedtls-$VERSION/library" -j"$JOBS" CC="${CC:-cc}" \
    CFLAGS="-O2" libmbedcrypto.a libmbedx509.a libmbedtls.a >/dev/null

mkdir -p "$PREFIX/lib"
cp -R "$WORK/mbedtls-$VERSION/include" "$PREFIX/"
cp "$WORK/mbedtls-$VERSION"/library/*.a "$PREFIX/lib/"
rm -rf "$WORK"
echo "Mbed TLS $VERSION installed in $PREFIX"
