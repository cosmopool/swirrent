#pragma once
#include "../include/arena.h"
#include "core.h"

#include <stdbool.h>

typedef enum : u8 {
  INT,
  STRING,
  LIST,
  DICT,
} BencodeType;

typedef struct {
  usize cursor;
  usize path_cursor;
  usize dict_stack_pos;
  usize dict_stack[128];
  char tmp_buff[128];
} BencodeParser;

typedef i64 BencodeNumber;
typedef String BencodeString;

void BencodeDecode(Arena *arena, BencodeParser *parser, const char *bencode_str,
                   usize len);

// dictionary
typedef struct {
  String key;
  void *value;
} BencodeDictionaryEntry;

typedef struct {
  void *entries;
} BencodeDictionary;

void BencodeDictSet(BencodeDictionary *dict, String key, void *value,
                    BencodeType type);
void *BencodeDictGet(BencodeDictionary *dict, String key);

typedef struct {
  union {
    usize num;
    String *string;
  };
} BencodeList;

typedef struct {
  BencodeType kind;
  union {
    BencodeNumber num;
    BencodeString string;
    BencodeList list;
    BencodeDictionary dictionary;
  };
} BencodeValue;
