#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bencode.h"

char bencodeParserCurrent(BencodeParser *decoder) {
  return decoder->bencode[decoder->cursor];
}

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
  printf("FILE SIZE: %luK\n", length / 1024);

  return bencodeParserFromData(content, length);
};

void bencodeParserCleanup(BencodeParser *bencode) {
  assert(bencode != NULL);
  free((void *)bencode->bencode);
}

String bencodeStringDecode(BencodeParser *decoder) {
  char *colon_ptr;
  errno = 0;
  isize str_len = strtol(&decoder->bencode[decoder->cursor], &colon_ptr, 10);
  if (errno) {
    char *msg = "[BAD STRING] Not able to decode string lenght: %s\n";
    fprintf(stderr, msg, strerror(errno));
    return (String){0};
  }
  if (str_len > 0) assert(decoder->bencode_len - decoder->cursor > (usize)str_len);

  String s = {.len = str_len, .data = colon_ptr + 1};
  // find where the string ended and set the cursor to that position
  decoder->cursor = (s.data + s.len) - decoder->bencode;
  return s;
}

isize bencodeIntegerDecode(BencodeParser *decoder) {
  assert(decoder->bencode_len - decoder->cursor > 3);
  char *end_ptr;

  const char *start_ptr = &decoder->bencode[decoder->cursor];
  isize integer = strtol(start_ptr + 1, &end_ptr, 10);
  if (errno) {
    printf("Not able to convert to integer: %s\n", strerror(errno));
    char *msg = "[BAD INTEGER] Not able to convert integer from: %*s\n";
    fprintf(stderr, msg, 25, start_ptr);
    exit(1);
  }

  // find where the string ended and set the cursor to that position
  decoder->cursor = end_ptr - decoder->bencode + 1;

  return integer;
}

void bencodeValueSkip(BencodeParser *decoder) {
  if (decoder->bencode_len <= 0) {
    fprintf(stderr, "Empty data! Nothing to parse.\n");
    exit(1);
  }

  while (decoder->cursor < decoder->bencode_len) {
    switch (bencodeParserCurrent(decoder)) {
    case 'i': {
      (void)bencodeIntegerDecode(decoder);
      return;
    }

    case '0' ... '9': {
      (void)bencodeStringDecode(decoder);
      return;
    }

    case 'l':
    case 'd': {
      char c = bencodeParserCurrent(decoder);
      assert(c == 'l' || c == 'd');
      decoder->cursor++;
      while (bencodeParserCurrent(decoder) != 'e') {
        bencodeValueSkip(decoder);
      }
      decoder->cursor++;
      return;
    }

    case 'e':
      decoder->cursor++;
      printf("cursor ended at %ld and string length is %ld", decoder->cursor, decoder->bencode_len);
      return;
    };
  }
}
