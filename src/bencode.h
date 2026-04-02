#pragma once

#include "core.h"

#define IS_LIST parser->bencode[parser->cursor] == 'l'
#define IS_DICT parser->bencode[parser->cursor] == 'd'
#define STRING_MATCHES(key, string)                                 \
  (string.data[0] == (key)[0] && string.len == (sizeof(key) - 1) && \
   memcmp(string.data, key, sizeof(key) - 1) == 0)

typedef enum : u8 {
  INT,
  STRING,
  LIST,
  DICT,
} BencodeType;

typedef struct {
  usize cursor;
  usize path_cursor;
  char *bencode;
  usize bencode_len;
} BencodeParser;

BencodeParser bencodeParserFromData(char *data, usize len);
BencodeParser bencodeParserFromFile(const char *file_path);
void bencodeParserCleanup(BencodeParser *bencode);

isize bencodeIntegerDecode(BencodeParser *p);
String bencodeStringDecode(BencodeParser *p);
void bencodeValueSkip(BencodeParser *p);
