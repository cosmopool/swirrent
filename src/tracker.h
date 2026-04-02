#pragma once

#include "bencode.h"

typedef struct {
  const char *data;
  usize len;
  usize count;
} TorrentPeers;

typedef struct {
  const char *data;
  usize len;
  usize count;
} TorrentPeers6;

typedef struct TorrentTrackerResponse {
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
  // usize peer_count;
} TrackerResponse;

void trackerResponseDecode(BencodeParser *p, TrackerResponse *out);
