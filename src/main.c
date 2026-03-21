#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "bencode.c"
#define STRING_IMPLEMENTATION
#include "core.h"
#include "downloader.c"
#include "torrent.c"

i32 main(i32 argc, char **argv) {
  u32 result = 0;
  bool verbose = false;
  bool only_decode = false;
  char *torrentFile = NULL;
  char *rawRequestFile = NULL;

  for (i32 i = 1; i < argc; i++) {
    if (i == 1) {
      if (!argv[i]) {
        printf("torrent file must be provided as argument!");
        exit(1);
      }
      torrentFile = argv[i];
      continue;
    }

    if (strncmp(argv[i], "--raw-response", 14) == 0) {
      i++;
      if (!argv[i]) {
        printf("raw request bin file must be provided as argument!");
        exit(1);
      }
      rawRequestFile = argv[i];
      continue;
    }

    if (strncmp(argv[i], "--decode-only", 13) == 0) {
      only_decode = true;
      continue;
    }

    if (strncmp(argv[i], "-v", 2) == 0 || strncmp(argv[i], "--verbose", 9) == 0) {
      verbose = true;
      continue;
    }
  }

  // decode torrent file
  BencodeParser decoder = bencodeParserFromFile(torrentFile);
  assert(decoder.bencode[decoder.cursor] == 'd');
  TorrentMetainfo *metainfo = torrentMetainfoInit();
  bencodeDecode(&decoder, metainfo);
  if (verbose) torrentMetainfoPrint(*metainfo);

  // encode info dictionary to hash it's value and save in metainfo->info_hash
  bencodeInfoDictEncode(*metainfo);

  if (only_decode) goto deinit;

  String rawRequest = {0};
  if (rawRequestFile) {
    FILE *file = fopen(rawRequestFile, "rb");
    if (!file) {
      perror("fopen");
      exit(1);
    }

    // calculate the file size
    fseek(file, 0, SEEK_END);
    rawRequest.len = ftell(file);
    rewind(file);

    // allocate and copy the file contents
    rawRequest.data = (char *)malloc(rawRequest.len * sizeof(char));
    fread((void *)rawRequest.data, rawRequest.len, 1, file);
    fclose(file);
  }

  if (!rawRequestFile) {
    // fetch peer list from available tracker
    result = downloaderTrackerPeerListFetch(metainfo);
  }

deinit:
  torrentMetainfoCleanup(metainfo);
  bencodeParserCleanup(&decoder);
  return result;
}
