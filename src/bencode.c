#include "bencode.h"
#include "linked_list.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ERROR_RESULT                                                           \
  (BencodeReturn_t) {                                                          \
    .kind = BENCODE_KIND_NONE, .data = NULL, .remainder = bencode              \
  }

BencodeKind parseBencodeKind(char c) {
  switch (c) {
  case 'i':
    return BENCODE_KIND_INT;
  case 'l':
    return BENCODE_KIND_LIST;
  case 'd':
    return BENCODE_KIND_DICT;
  case '0' ... '9':
    return BENCODE_KIND_STRING;
  };

  return BENCODE_KIND_NONE;
}

BencodeReturn_t bencode_decode(Arena *arena, const char *bencode, usize len) {
  assert(arena);

  if (len <= 0) {
    return ERROR_RESULT;
  }

  switch (parseBencodeKind(bencode[0])) {
  case BENCODE_KIND_INT: {
    if (len < 3) {
      char *msg = "[BAD INTEGER] Not enough information to decode: %*s\n";
      fprintf(stderr, msg, 5, bencode);
      return ERROR_RESULT;
    }

    char *end_ptr = strchr(&bencode[1], 'e');
    if (!end_ptr) {
      char *msg =
          "[BAD INTEGER] Did not find the enclosing 'e' in string: %*s\n";
      fprintf(stderr, msg, 5, bencode);
      return ERROR_RESULT;
    }

    isize end_idx = end_ptr - bencode;
    isize integer = strtol(&bencode[1], NULL, 10);
    if (errno) {
      char *msg = "[BAD INTEGER] Not able to convert integer from: %*s\n";
      fprintf(stderr, msg, end_idx, bencode);
      return ERROR_RESULT;
    }

    void *integer_ptr = arena_alloc(arena, sizeof(usize));
    *(usize *)integer_ptr = integer;
    return (BencodeReturn_t){
        .kind = BENCODE_KIND_INT,
        .data = integer_ptr,
        .remainder = &bencode[end_idx + 1],
    };
  }

  case BENCODE_KIND_STRING: {
    char *colon_ptr = strchr(&bencode[1], ':');
    if (!colon_ptr) {
      char *msg = "[BAD INTEGER] Did not find ':' in string: %*s...\n";
      fprintf(stderr, msg, 5, bencode);
      return ERROR_RESULT;
    }

    isize colon_idx = colon_ptr - bencode;
    isize str_len = strtol(bencode, NULL, 10);
    if (errno) {
      char *msg = "[BAD STRING] Not able to decode string lenght: %s\n";
      fprintf(stderr, msg, bencode);
      return ERROR_RESULT;
    }

    void *str = arena_alloc(arena, sizeof(const char *) * str_len);
    memcpy(str, &bencode[colon_idx + 1], str_len);
    return (BencodeReturn_t){
        .kind = BENCODE_KIND_STRING,
        .data = str,
        .remainder = &bencode[colon_idx + str_len + 1],
    };
  }

  case BENCODE_KIND_LIST: {
    ListNode_t *list = listInit(arena);
    const char *remainder = &bencode[1];
    usize new_len = len - 1;
    while (!remainder[0] || !(remainder[0] == 'e')) {
      BencodeReturn_t result = bencode_decode(arena, remainder, new_len);
      remainder = result.remainder;

      switch (result.kind) {
      case BENCODE_KIND_INT:
        break;
      case BENCODE_KIND_STRING:
        break;
      case BENCODE_KIND_DICT:
        printf("not implemented\n");
        exit(1);
        break;
      case BENCODE_KIND_NONE:
        return ERROR_RESULT;
      default:
        break;
      };

      void *value = arena_alloc(arena, sizeof(BencodeValue_t));
      *(BencodeValue_t *)value =
          (BencodeValue_t){.kind = result.kind, .data = result.data};
      listAppend(arena, list, value);
    }

    return (BencodeReturn_t){
        .kind = BENCODE_KIND_LIST,
        .data = list,
        .remainder = remainder,
    };
  }

  case BENCODE_KIND_DICT: {
    return ERROR_RESULT;
  }

  case BENCODE_KIND_NONE:
    break;
  };

  fprintf(stderr, "[BAD STRING] Bad bencoded string: %s\n", bencode);
  return ERROR_RESULT;
}
