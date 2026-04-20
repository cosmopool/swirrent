#!/bin/sh

CFLAGS="-g -O0 -Wall -Werror -Wcast-align -Wunreachable-code -flto"
INCLUDES="-lcurl"
SOURCES="src/unix/* src/swirrent.c src/bencode.c src/torrent.c src/tracker.c src/asio.c src/log.c src/peer.c"
echo "clang -o swirrent $CFLAGS $INCLUDES $SOURCES"
clang -o swirrent $CFLAGS $INCLUDES $SOURCES
