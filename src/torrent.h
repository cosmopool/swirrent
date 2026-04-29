#pragma once

#include "bencode.h"
#include "core.h"
#include "metainfo.h"

#include <assert.h>

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
