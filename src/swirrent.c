#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asio.h"
#include "bencode.h"
#include "core.h"
#include "log.h"
#include "peer.h"
#include "swirrent.h"
#include "threads.h"
#include "torrent.h"
#include "tracker.h"

SwirrentContext swirrentInit(SwirrentOptions options) {
  SwirrentContext ctx = {
      .options = options,
      .metainfo = torrentMetainfoInit(),
      .parser = bencodeParserFromFile(options.torrent_path),
  };
  logInit(ctx.options.log_enabled);
  logSetOutputPath(ctx.options.log_output_path);
  // trackerOptionsSet(&ctx.options);
  logInfo("initializing threads");
  if (threadsPoolInit() > 0) logInfo("failed to init threads");
  return ctx;
}

void swirrentShutdown(SwirrentContext *ctx) {
  logInfo("deinitializing metainfo");
  torrentMetainfoCleanup(ctx->metainfo);
  logInfo("deinitializing bencode parser");
  bencodeParserCleanup(&ctx->parser);
  logInfo("deinitializing threads");
  if (threadsPoolDeinit() > 0) logInfo("failed to deinit threads");
}

void swirrentHandshake(SwirrentContext *ctx) {
  logInfo("target peer: %s", ctx->options.peer_address);
  logInfo("performing peer handshake");
  u8 peer_id[20] = {"-MY0001-"};
  for (int i = 8; i < 20; i++)
    peer_id[i] = rand() & 0xff;
  logInfo("current peer id: %s", peer_id);

  u8 ip_str[INET_ADDRSTRLEN] = {0};
  u8 c;
  u16 i = 0;
  while ((c = ctx->options.peer_address[i]) != '\0') {
    if (c == ':') break;
    ip_str[i] = ctx->options.peer_address[i];
    i++;
  }

  struct sockaddr_in peer_addr = {
      .sin_port = atoi(ctx->options.peer_address + i + 1),
      .sin_family = AF_INET,
  };
  i32 r = inet_pton(AF_INET, (const char *)ip_str, &(peer_addr.sin_addr));
  if (r == 0) return logError("invalid ip format: %s", ip_str);
  if (r < 0) return logError("invalid ip: %s", strerror(errno));
  logInfo("pieces: %lu", ctx->metainfo->info.pieces_count);
  peerAdd((u8 *)&peer_addr.sin_addr, peer_addr.sin_port, IPV4_LEN, ctx->metainfo->info_hash, peer_id);
  asioWaitForEvents();
  // peerLoop();
}

i32 swirrentDecodeMetainfo(SwirrentContext *ctx) {
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
  return 0;
}

i32 swirrentMain(SwirrentContext *ctx) {
  i32 decode_result = swirrentDecodeMetainfo(ctx);
  if (decode_result < 0) return decode_result;

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
    // Peer4 peer = peerGet(resp.peers.data, 0);
    // result = peer4Handshake(peer, ctx->metainfo->info_hash, peer_id);
    // result = peer6Handshake(resp.peers6[0], ctx->metainfo->info_hash, peer_id);
    logInfo("finished generating peer handshake, result: %d", result);
    if (result != 0) return result;
  } else {
    logInfo("no dump response. starting fresh.");
    logInfo("fetching peer list from (%lu) trackers", ctx->metainfo->trackers_count);
    // no raw request was load, so we will talk to trackers for peers
    i32 result = trackerPeerListFetch(ctx->metainfo->trackers_url, ctx->metainfo->trackers_count, ctx->metainfo->info_hash, peer_id, peerAddMany);
    logInfo("finish fetching peer list, result: %d", result);
    if (result != 0) return result;
  }
  return 0;
}
