#include "../include/arena.h"
#include "core.h"

#include <stdbool.h>

typedef enum : u8 {
  BENCODE_KIND_INT,
  BENCODE_KIND_STRING,
  BENCODE_KIND_LIST,
  BENCODE_KIND_DICT,
  BENCODE_KIND_NONE
} BencodeKind;

typedef struct {
  BencodeKind kind;
  void *data;
} BencodeValue_t;

typedef struct BencodeReturn {
  BencodeKind kind;
  void *data;
  const char *remainder;
} BencodeReturn_t;

BencodeReturn_t bencode_decode(Arena *arena, const char *bencode, usize len);
