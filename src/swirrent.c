#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define STRING_IMPLEMENTATION
#include "core.h"
#include "swirrent.h"

#include "bencode.h"
#include "swirrent.h"
#include "torrent.h"

SwirrentContext swirrentInit(SwirrentOptions options) {
  return (SwirrentContext){
      .options = options,
      .metainfo = torrentMetainfoInit(),
      .parser = bencodeParserFromFile(options.torrent_path),
  };
}

i32 swirrentMain(SwirrentContext *ctx) {
  BencodeParser decoder = ctx->parser;
  // decode torrent file
  assert(decoder.bencode[decoder.cursor] == 'd');
  torrentMetainfoDecode(&decoder, ctx->metainfo);
  if (ctx->options.verbose) torrentMetainfoPrint(*ctx->metainfo);
  return 0;
}

void swirrentShutdown(SwirrentContext *ctx) {
  torrentMetainfoCleanup(ctx->metainfo);
  bencodeParserCleanup(&ctx->parser);
}
