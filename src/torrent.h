#pragma once

#include "bencode.h"
#include "core.h"

#include <assert.h>
#include <stdbool.h>

typedef struct TorrentFile {
  usize length;
  usize path_count;
  String *path; // Array of path components
} TorrentFile;

typedef struct TorrentInfoFiles {
  // files list - array of file dictionaries
  TorrentFile *files;
  String *paths;
  usize count;
} TorrentInfoFiles;

typedef struct {
  char *data;
  usize len;
  usize count;
} TorrentPeers;

typedef struct {
  const char *data;
  usize len;
  usize count;
} TorrentPeers6;

typedef struct TorrentInfo {
  // Discriminator: true for single file, false for multi-file
  bool is_single_file;

  // The name key maps to a UTF-8 encoded string which is the suggested name to
  // save the file (or directory) as. It is purely advisory.
  String name;

  // piece length maps to the number of bytes in each piece the file is split
  // into. For the purposes of transfer, files are split into fixed-size pieces
  // which are all the same length except for possibly the last one which may be
  // truncated. piece length is almost always a power of two, most commonly 2 18
  // = 256 K (BitTorrent prior to version 3.2 uses 2 20 = 1 M as default).
  usize piece_length;

  // pieces maps to a string whose length is a multiple of 20. It is to be
  // subdivided into strings of length 20, each of which is the SHA1 hash of the
  // piece at the corresponding index.
  String pieces;

  union {
    // length - The length of the file, in bytes.
    usize length;
    TorrentInfoFiles multi_files;
  };
} TorrentInfo;

typedef struct TorrentMetainfo {
  // The URL of the tracker.
  String announce;
  String *trackers_url;
  usize trackers_count;
  TorrentInfo info;
  u8 info_hash[20];
} TorrentMetainfo;

typedef struct {
  // Tracker response fields
  // failure reason - optional human readable string explaining why the query
  // failed
  String failure_reason;
  String warning_message;

  // interval - number of seconds the downloader should wait between regular
  // rerequests
  usize interval;
  usize min_interval;

  usize complete;
  usize downloaded;
  usize incomplete;

  // peers - list of dictionaries corresponding to peers
  TorrentPeers peers;
  TorrentPeers6 peers6;

  u64 connection_id;
  // usize peer_count;
} TorrentTrackerResponse;

TorrentMetainfo *torrentMetainfoInit();
void torrentMetainfoCleanup(TorrentMetainfo *mi);
void torrentMetainfoPrint(TorrentMetainfo metainfo);
void torrentInfoMultiFileSet(TorrentInfo *info);
void torrentPieceHashGet(usize piece_idx, TorrentMetainfo *m, char *hash_out);

void torrentMetainfoDecode(BencodeParser *p, TorrentMetainfo *out);

void torrentDictDecode(BencodeParser *p, TorrentMetainfo *out);
void torrentListDecode(BencodeParser *p, TorrentMetainfo *out);
void torrentInfoDictDecode(BencodeParser *p, TorrentMetainfo *out);

void torrentInfoDictEncode(TorrentMetainfo *m);
u32 torrentResponseDecode(String *raw_resp, TorrentTrackerResponse *resp);
void torrentInfoHashGenerate(TorrentMetainfo *m);
