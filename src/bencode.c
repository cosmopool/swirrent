#include "../include/arena.h"
#include "core.h"

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum : u8 {
  BENCODE_TYPE_INT,
  BENCODE_TYPE_STRING,
  BENCODE_TYPE_LIST,
  BENCODE_TYPE_DICT,
  BENCODE_TYPE_NONE
} BencodeValueType;

typedef struct {
  BencodeValueType type;
  void *value;
} BencodeValue;

BencodeValueType parseBencodeValueType(char c) {
  switch (c) {
  case 'i':
    return BENCODE_TYPE_INT;
  case 'l':
    return BENCODE_TYPE_LIST;
  case 'd':
    return BENCODE_TYPE_DICT;
  case '0' ... '9':
    return BENCODE_TYPE_STRING;
  };

  return BENCODE_TYPE_NONE;
}

i32 bencode_decode(Arena *arena, const char *bencode, usize len) {
  assert(arena);

  printf("==>> len: %ld\n", len);
  if (len <= 0) {
    return 0;
  }

  switch (parseBencodeValueType(bencode[0])) {
  case BENCODE_TYPE_INT: {
    if (len < 3) {
      char *msg = "[BAD INTEGER] Not enough information to decode: %*s\n";
      fprintf(stderr, msg, 5, bencode);
      return -1;
    }

    char *end_ptr = strchr(&bencode[1], 'e');
    if (!end_ptr) {
      char *msg =
          "[BAD INTEGER] Did not find the enclosing 'e' in string: %*s\n";
      fprintf(stderr, msg, 5, bencode);
      return -1;
    }

    isize end_idx = end_ptr - bencode;
    isize integer = strtol(&bencode[1], NULL, 10);
    if (errno) {
      char *msg = "[BAD INTEGER] Not able to convert integer from: %*s\n";
      fprintf(stderr, msg, end_idx, bencode);
      return -1;
    }

    printf("%ld\n", integer);
    return bencode_decode(arena, &bencode[end_idx + 1], len - 1 - end_idx);
  }

  case BENCODE_TYPE_STRING: {
    char *colon_ptr = strchr(&bencode[1], ':');
    if (!colon_ptr) {
      char *msg = "[BAD INTEGER] Did not find ':' in string: %*s...\n";
      fprintf(stderr, msg, 5, bencode);
      return -1;
    }

    isize colon_idx = colon_ptr - bencode;
    isize str_len = strtol(bencode, NULL, 10);
    if (errno) {
      char *msg = "[BAD STRING] Not able to decode string lenght: %s\n";
      fprintf(stderr, msg, bencode);
      return -1;
    }

    const char *str = &bencode[colon_idx + 1];
    printf("%s\n", str);
    if (str_len + colon_idx + 1) return 0;
    return bencode_decode(arena, &bencode[colon_idx + str_len + 1],
                          len - 1 - str_len);
  }

  case BENCODE_TYPE_LIST: {
    return -1;
  }

  case BENCODE_TYPE_DICT: {
    return -1;
  }

  case BENCODE_TYPE_NONE:
    break;
  };

  fprintf(stderr, "[BAD STRING] Bad bencoded string: %s\n", bencode);
  return -1;
}
