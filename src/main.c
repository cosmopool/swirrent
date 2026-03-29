#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "bencode.c"
#include "bencode.h"
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

    else if (strncmp(argv[i], "--load-response", 15) == 0 || strncmp(argv[i], "-l", 2) == 0) {
      if (options.raw_request_output_path) {
        printf("you must specify only one of those: '--load-response' or '--dump-response'");
        return 1;
      }
      i++;
      if (!argv[i]) {
        printf("raw request bin file must be provided as argument!");
        return 1;
      }
      options.raw_request_path = argv[i];
      continue;
    }

    else if (strncmp(argv[i], "--dump-response", 15) == 0 || strncmp(argv[i], "-d", 2) == 0) {
      if (options.raw_request_output_path) {
        printf("you must specify only one of those: '--load-response' or '--dump-response'");
        return 1;
      }
      i++;
      if (!argv[i]) {
        printf("output file for the response dump must be provided as argument!");
        exit(1);
      }
      options.dump_response = true;
      options.raw_request_output_path = argv[i];
      continue;
    }

    else if (strncmp(argv[i], "--decode-only", 13) == 0) {
      options.decode_only = true;
      continue;
    }

    else if (strncmp(argv[i], "-v", 2) == 0 || strncmp(argv[i], "--verbose", 9) == 0) {
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
  bencodeInfoDictEncode(metainfo);

  if (options.decode_only) {
    torrentMetainfoCleanup(metainfo);
    bencodeParserCleanup(&decoder);
    return 1;
  }

  String raw_request = {0};
  if (options.raw_request_path) {
    // populate
    FILE *file = fopen(options.raw_request_path, "rb");
    if (!file) {
      perror("fopen");
      torrentMetainfoCleanup(metainfo);
      bencodeParserCleanup(&decoder);
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

  bool was_raw_request_loaded = options.raw_request_path != 0;
  if (was_raw_request_loaded) {
    // raw request was load, so we jsut decode it to access the peer list
    TorrentTrackerResponse resp = {0};
    result = downloaderTrackerResponseDecode(raw_request, metainfo, &resp);
    if (result != 0) return result;

    u8 peer_id[20] = {0};
    // 8-byte prefix: Azureus-style "-MY0001-"
    memcpy(peer_id, "-MY0001-", 8);
    for (int i = 8; i < 20; i++)
      peer_id[i] = rand() & 0xff;

    result = downloaderPeerHandshake(&resp, metainfo->info_hash, peer_id);
    if (result != 0) return result;
  } else {
    // no raw request was load, so we will talk to trackers for peers
    downloaderOptionsSet(&options);
    // fetch peer list from available tracker
    TorrentTrackerResponse resp = {0};
    result = downloaderTrackerPeerListFetch(metainfo, &resp);
  }

  torrentMetainfoCleanup(metainfo);
  bencodeParserCleanup(&decoder);
  return result;
}
