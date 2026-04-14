#!/bin/sh

CFLAGS="-g -O0 -Wall -Werror -Wcast-align -Wunreachable-code"
INCLUDES="-lcurl"
SOURCES="src/unix/* src/swirrent.c src/bencode.c src/torrent.c src/tracker.c src/asio.c src/log.c src/peer.c"
echo "clang -o decoder $CFLAGS $INCLUDES $SOURCES"
clang -o decoder $CFLAGS $INCLUDES $SOURCES
