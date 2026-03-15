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
typedef struct {
  void *entries;
} BencodeDictionary;

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

void bencodeInfoDictEncode(TorrentMetainfo metainfo);
BencodeValue bencodeDecode(BencodeParser *parser, String bencode,
                            TorrentMetainfo *metainfo);
isize bencodeIntegerDecode(BencodeParser *parser, String bencode);
String bencodeStringDecode(BencodeParser *parser, String bencode);
BencodeValue bencodeDictDecode(BencodeParser *parser, String bencode,
                                TorrentMetainfo *metainfo);
BencodeValue bencodeListDecode(BencodeParser *parser, String bencode,
                                TorrentMetainfo *metainfo);
BencodeValue bencodeInfoDictDecode(BencodeParser *parser, String bencode,
                                    TorrentMetainfo *metainfo);
