#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bencode.h"
#include "core.h"

BencodeParser bencodeParserFromData(char *data, usize len) {
  assert(data);
  assert(len > 0);
  BencodeParser parser = {0};
  parser.bencode = data;
  parser.bencode_len = len;
  return parser;
};

BencodeParser bencodeParserFromFile(const char *path) {
  printf("FILE: %s\n", path);
  FILE *file = fopen(path, "rb");
  if (!file) {
    perror("fopen");
    exit(1);
  }

  // calculate the file size
  fseek(file, 0, SEEK_END);
  u64 length = ftell(file);
  assert(length > 0);
  rewind(file);

  // allocate and copy the file contents
  char *content = (char *)malloc(length * sizeof(char));
  assert(fread(content, length, 1, file) > 0);
  fclose(file);
  printf("FILE SIZE: %lluK\n", length / 1024);

  return bencodeParserFromData(content, length);
};

void bencodeParserCleanup(BencodeParser *bencode) {
  assert(bencode != NULL);
  free((void *)bencode->bencode);
}

String bencodeStringDecode(BencodeParser *parser) {
  char *colon_ptr;
  errno = 0;
  isize str_len = strtol(&parser->bencode[parser->cursor], &colon_ptr, 10);
  if (errno) {
    char *msg = "[BAD STRING] Not able to decode string lenght: %s\n";
    fprintf(stderr, msg, strerror(errno));
    exit(1);
  }
  if (str_len > 0) assert(parser->bencode_len - parser->cursor > (usize)str_len);

  String s = {.len = str_len, .data = colon_ptr + 1};
  // find where the string ended and set the cursor to that position
  parser->cursor = (s.data + s.len) - parser->bencode;
  return s;
}

isize bencodeIntegerDecode(BencodeParser *parser) {
  assert(parser->bencode_len - parser->cursor > 3);
  char *end_ptr;

  const char *start_ptr = &parser->bencode[parser->cursor];
  isize integer = strtol(start_ptr + 1, &end_ptr, 10);
  if (errno) {
    printf("Not able to convert to integer: %s\n", strerror(errno));
    char *msg = "[BAD INTEGER] Not able to convert integer from: %*s\n";
    fprintf(stderr, msg, 25, start_ptr);
    exit(1);
  }

  // find where the string ended and set the cursor to that position
  parser->cursor = end_ptr - parser->bencode + 1;

  return integer;
}

void bencodeValueSkip(BencodeParser *parser) {
  if (parser->bencode_len <= 0) {
    fprintf(stderr, "Empty data! Nothing to parse.\n");
    exit(1);
  }

  while (parser->cursor < parser->bencode_len) {
    switch (parser->bencode[parser->cursor]) {
    case 'i': {
      (void)bencodeIntegerDecode(parser);
      return;
    }

    case '0' ... '9': {
      (void)bencodeStringDecode(parser);
      return;
    }

    case 'l':
    case 'd': {
      assert(IS_LIST || IS_DICT);
      parser->cursor++;
      while (parser->bencode[parser->cursor] != 'e') {
        bencodeValueSkip(parser);
      }
      parser->cursor++;
      return;
    }

    case 'e':
      parser->cursor++;
      printf("cursor ended at %ld and string length is %ld", parser->cursor, parser->bencode_len);
      return;
    };
  }
}
