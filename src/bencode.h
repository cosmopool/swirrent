#pragma once
#include "core.h"
#include "torrent.h"

#define SHA_DIGEST_LENGTH 20
#define PEER_ID_LENGTH 20

typedef enum : u8 {
  INT,
  STRING,
  LIST,
  DICT,
} BencodeType;

typedef struct {
  usize cursor;
  usize path_cursor;
  char *bencode;
  usize bencode_len;
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

BencodeParser bencodeParserFromData(char *data, usize len);
BencodeParser bencodeParserFromFile(const char *file_path);
void bencodeParserCleanup(BencodeParser *bencode);

void bencodeInfoDictEncode(TorrentMetainfo *m);
BencodeValue bencodeDecode(BencodeParser *p, TorrentMetainfo *m);
isize bencodeIntegerDecode(BencodeParser *p);
String bencodeStringDecode(BencodeParser *p);
BencodeValue bencodeDictDecode(BencodeParser *p, TorrentMetainfo *m);
BencodeValue bencodeListDecode(BencodeParser *p, TorrentMetainfo *m);
BencodeValue bencodeInfoDictDecode(BencodeParser *p, TorrentMetainfo *m);
BencodeValue bencodeTrackerResponseDecode(BencodeParser *p, TorrentMetainfo *m, TorrentTrackerResponse *r);
