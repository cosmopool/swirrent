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
  printf("FILE: %s\n", argv[1]);
  FILE *file_ptr = fopen(argv[1], "rb");
  if (!file_ptr) {
    perror("fopen");
    exit(1);
  }

  // calculate the file size
  fseek(file_ptr, 0, SEEK_END);
  u64 file_length = ftell(file_ptr);
  rewind(file_ptr);

  // allocate and copy the file contents
  char *file_content = (char *)malloc(file_length * sizeof(char));
  fread(file_content, file_length, 1, file_ptr);
  fclose(file_ptr);
  printf("FILE SIZE: %luK\n", file_length / 1024);

  String torrent_file_content = {
      .len = file_length,
      .data = file_content,
  };

  // decode torrent file
  BencodeParser decoder = {0};
  assert(torrent_file_content.data[decoder.cursor] == 'd');
  TorrentMetainfo *metainfo = torrentMetainfoInit();
  bencodeDecode(&decoder, torrent_file_content, metainfo);
  if (argv[2] && memcmp(&argv[2], "-v", 2)) torrentMetainfoPrint(*metainfo);

  // encode info dictionary to hash it's value
  bencodeInfoDictEncode(*metainfo);

  // fetch peer list from available tracker
  // return 0;
  u32 result = downloaderTrackerPeerListFetch(metainfo);
  torrentMetainfoCleanup(metainfo);
  return (int)result;
}
