#pragma once

#include <stdbool.h>

#include "bencode.h"
#include "core.h"
#include "torrent.h"

typedef struct {
  bool verbose;
  bool decode_only;
  bool dump_response;
  char *torrent_path;
  char *raw_request_path;
  char *raw_request_output_path;
  char *log_output_path;
} SwirrentOptions;

typedef struct {
  BencodeParser parser;
  TorrentMetainfo *metainfo;
  SwirrentOptions options;
} SwirrentContext;

SwirrentContext swirrentInit(SwirrentOptions);
i32 swirrentMain(SwirrentContext *);
void swirrentShutdown(SwirrentContext *);
void swirrentPrintMemoryUtilization(u64 *total, u64 *used);
