#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "bencode.c"
#define STRING_IMPLEMENTATION
#include "core.h"
#include "downloader.c"
#include "torrent.c"

i32 main(i32 argc, char **argv) {
  (void)argc;

  if (!argv[1]) {
    printf("a torrent file must be provided as argument");
    exit(1);
  }

  // decode torrent file
  BencodeParser decoder = bencodeParserFromFile(argv[1]);
  assert(decoder.bencode[decoder.cursor] == 'd');
  TorrentMetainfo *metainfo = torrentMetainfoInit();
  bencodeDecode(&decoder, metainfo);
  if (argv[2] && memcmp(&argv[2], "-v", 2)) torrentMetainfoPrint(*metainfo);

  // encode info dictionary to hash it's value
  bencodeInfoDictEncode(*metainfo);

  // return 0;

  // fetch peer list from available tracker
  u32 result = downloaderTrackerPeerListFetch(metainfo);
  torrentMetainfoCleanup(metainfo);
  bencodeParserCleanup(&decoder);
  return (int)result;
}
