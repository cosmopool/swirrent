#pragma once

#include "core.h"

#define IS_LIST(x) (x) == 'l'
#define IS_DICT(x) (x) == 'd'
#define STRING_MATCHES(key, string)                                 \
  (string.data[0] == (key)[0] && string.len == (sizeof(key) - 1) && \
   memcmp(string.data, key, sizeof(key) - 1) == 0)

typedef struct {
  usize cursor;
  usize path_cursor;
  char *bencode;
  usize bencode_len;
} BencodeParser;

char bencodeParserCurrent(BencodeParser *);

BencodeParser bencodeParserFromData(char *data, usize len);
BencodeParser bencodeParserFromFile(const char *file_path);
void bencodeParserCleanup(BencodeParser *bencode);

isize bencodeIntegerDecode(BencodeParser *p);
String bencodeStringDecode(BencodeParser *p);
void bencodeValueSkip(BencodeParser *p);
