#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bencode.h"
#include "core.h"
#include "log.h"
#include "peer.h"
#include "swirrent.h"
#include "threads.h"
#include "torrent.h"
#include "tracker.h"

SwirrentContext swirrentInit(SwirrentOptions options) {
  trackerOptionsSet(&options);
  return (SwirrentContext){
      .options = options,
      .metainfo = torrentMetainfoInit(),
      .parser = bencodeParserFromFile(options.torrent_path),
  };
}

void swirrentShutdown(SwirrentContext *ctx) {
  torrentMetainfoCleanup(ctx->metainfo);
  bencodeParserCleanup(&ctx->parser);
}

i32 swirrentMain(SwirrentContext *ctx) {
  logInit(ctx->options.log_enabled);
  logSetOutputPath(ctx->options.log_output_path);
  logInfo("initializing threads");
  if (threadsPoolInit() > 0) {
    logInfo("failed to init threads");
  }
  BencodeParser decoder = ctx->parser;
  // decode torrent file
  assert(decoder.bencode[decoder.cursor] == 'd');
  torrentMetainfoDecode(&decoder, ctx->metainfo);
  if (ctx->metainfo->trackers_count == 0) {
    logInfo("trackerless torrents are not implemented yet.");
    return 1;
  }
  torrentInfoHashGenerate(ctx->metainfo);
  if (ctx->options.verbose) torrentMetainfoPrint(*ctx->metainfo);

  String raw_request = {0};
  if (ctx->options.raw_request_path) {
    logInfo("saving response in: %s", ctx->options.raw_request_path);
    FILE *file = fopen(ctx->options.raw_request_path, "rb");
    if (!file) {
      perror("fopen");
      return 1;
    }

    // calculate the file size
    fseek(file, 0, SEEK_END);
    raw_request.len = ftell(file);
    rewind(file);

    // allocate and copy the file contents
    raw_request.data = (char *)malloc(raw_request.len * sizeof(char));
    fread((void *)raw_request.data, raw_request.len, 1, file);
    fclose(file);
  }

  u8 peer_id[20] = {0};
  // 8-byte prefix: Azureus-style "-MY0001-"
  memcpy(peer_id, "-MY0001-", 8);
  for (int i = 8; i < 20; i++)
    peer_id[i] = rand() & 0xff;

  bool was_raw_request_loaded = ctx->options.raw_request_path != 0;
  if (was_raw_request_loaded) {
    logInfo("loading response dump");
    // raw request was load, so we jsut decode it to access the peer list
    TorrentTrackerResponse resp = {0};
    i32 result = torrentResponseDecode(&raw_request, &resp);
    logInfo("finished decoding tracker response, result: %d", result);
    if (result != 0) return result;

    logInfo("fetching peer list from (%lu) trackers", ctx->metainfo->trackers_count);
    result = peer6Handshake(&resp, ctx->metainfo->info_hash, peer_id);
    logInfo("finished generating peer handshake, result: %d", result);
    if (result != 0) return result;
  } else {
    logInfo("no dump response. starting fresh.");
    logInfo("fetching peer list from (%lu) trackers", ctx->metainfo->trackers_count);
    // no raw request was load, so we will talk to trackers for peers
    TorrentTrackerResponse resp = {0};
    i32 result = trackerPeerListFetch(ctx->metainfo, &resp, peer_id);
    logInfo("finish fetching peer list, result: %d", result);
    if (result != 0) return result;
  }

  logInfo("deinitializing threads");
  if (threadsPoolDeinit() > 0) {
    logInfo("failed to deinit threads");
  }
  return 0;
}
