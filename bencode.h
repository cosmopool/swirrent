#pragma once
#include "core.h"
#include "torrent.h"

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

BencodeValue bencodeDecode(BencodeParser *parser, String bencode);
isize decodeInteger(BencodeParser *parser, String bencode);
String decodeString(BencodeParser *parser, String bencode);
BencodeValue decodeDict(BencodeParser *parser, String bencode);
BencodeValue decodeList(BencodeParser *parser, String bencode);
BencodeValue decodeInfoDict(BencodeParser *parser, String bencode);
void bencodeEncodeInfoSHA1(TorrentInfo info);
