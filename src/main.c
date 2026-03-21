#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "bencode.c"
#include "torrent.h"
#define STRING_IMPLEMENTATION
#include "core.h"
#include "downloader.c"
#include "torrent.c"

i32 main(i32 argc, char **argv) {
  u32 result = 0;
  Options options = {0};

  for (i32 i = 1; i < argc; i++) {
    if (i == 1) {
      if (!argv[i]) {
        printf("torrent file must be provided as argument!");
        exit(1);
      }
      options.torrent_path = argv[i];
      continue;
    }

    if (strncmp(argv[i], "--raw-response", 14) == 0) {
      assert(!options.raw_request_output_path);
      i++;
      if (!argv[i]) {
        printf("raw request bin file must be provided as argument!");
        exit(1);
      }
      options.raw_request_path = argv[i];
      continue;
    }

    if (strncmp(argv[i], "--decode-only", 13) == 0) {
      options.decode_only = true;
      continue;
    }

    if (strncmp(argv[i], "-v", 2) == 0 || strncmp(argv[i], "--verbose", 9) == 0) {
      options.verbose = true;
      continue;
    }
  }

  // decode torrent file
  BencodeParser decoder = bencodeParserFromFile(options.torrent_path);
  assert(decoder.bencode[decoder.cursor] == 'd');
  TorrentMetainfo *metainfo = torrentMetainfoInit();
  bencodeDecode(&decoder, metainfo);
  if (options.verbose) torrentMetainfoPrint(*metainfo);

  // encode info dictionary to hash it's value and save in metainfo->info_hash
  bencodeInfoDictEncode(*metainfo);

  if (options.decode_only) goto deinit;

  String raw_request = {0};
  if (options.raw_request_path) {
    // populate
    FILE *file = fopen(options.raw_request_path, "rb");
    if (!file) {
      perror("fopen");
      exit(1);
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

  bool was_raw_request_loaded = options.raw_request_path != 0;
  if (was_raw_request_loaded) {
    // raw request was load, so we jsut decode it to access the peer list
    TorrentTrackerResponse resp = {0};
    result = downloaderTrackerResponseDecode(raw_request, metainfo, &resp);
  } else {
    // no raw request was load, so we will talk to trackers for peers
    downloaderOptionsSet(&options);
    // fetch peer list from available tracker
    result = downloaderTrackerPeerListFetch(metainfo);
  }

deinit:
  torrentMetainfoCleanup(metainfo);
  bencodeParserCleanup(&decoder);
  return result;
}
