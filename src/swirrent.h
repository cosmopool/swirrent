#pragma once

#include <stdbool.h>

#include "bencode.h"
#include "core.h"
#include "torrent.h"

typedef struct {
  bool verbose;
  bool decode_only;
  bool dump_response;
  bool log_enabled;
  char *torrent_path;
  char *raw_request_path;
  char *raw_request_output_path;
  char *log_output_path;
  char *peer_address;
} SwirrentOptions;

typedef struct {
  BencodeParser parser;
  TorrentMetainfo *metainfo;
  SwirrentOptions options;
} SwirrentContext;

SwirrentContext swirrentInit(SwirrentOptions);
i32 swirrentMain(SwirrentContext *);
i32 swirrentDecodeMetainfo(SwirrentContext *);
void swirrentHandshake(SwirrentContext *);
void swirrentShutdown(SwirrentContext *);
void swirrentPrintMemoryUtilization(u64 *total, u64 *used);
