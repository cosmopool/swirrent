#include "../include/arena.h"
#include "core.h"

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum : u8 { INT, STRING, LIST, DICT } BencodeValueType;

typedef struct {
  BencodeValueType type;
  void *value;
} BencodeValue;

bool is_int(char c) {
  return c == 'i';
}

bool is_string(char c) {
  return c == 's';
}

bool is_list(char c) {
  return c == 'l';
}

bool is_dict(char c) {
  return c == 'd';
}

bool is_end(char c) {
  return c == 'e';
}

i32 bencode_decode(Arena *arena, const char *bencode, usize len) {
  assert(arena);

  if (len == 0) {
    return 0;
  }

  u8 character = bencode[0];
  if (is_int(character)) {
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
    bencode_decode(arena, &bencode[end_idx + 1], len - end_idx);
  } else {
    for (usize i = 0; i < len; i++) {
      if (bencode[i] == 'e') return 0;
    }
  };

  return 0;
}
