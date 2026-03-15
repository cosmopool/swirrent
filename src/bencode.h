#pragma once
#include "core.h"
#include "torrent.h"

#define SHA_DIGEST_LENGTH 20

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

void bencodeInfoDictEncode(TorrentMetainfo m);
BencodeValue bencodeDecode(BencodeParser *p, String bencode, TorrentMetainfo *m);
isize bencodeIntegerDecode(BencodeParser *p, String bencode);
String bencodeStringDecode(BencodeParser *p, String bencode);
BencodeValue bencodeDictDecode(BencodeParser *p, String bencode, TorrentMetainfo *m);
BencodeValue bencodeListDecode(BencodeParser *p, String bencode, TorrentMetainfo *m);
BencodeValue bencodeInfoDictDecode(BencodeParser *p, String bencode, TorrentMetainfo *m);
BencodeValue bencodeTrackerResponseDecode(BencodeParser *p, String bencode, TorrentMetainfo *m, TorrentTrackerResponse *r);
