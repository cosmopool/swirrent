#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "core.h"
#include "torrent.h"

void torrentPieceHashGet(usize piece_idx, TorrentMetainfo *metainfo, char *hash) {
  usize real_idx = piece_idx * 20;
  for (u32 i = 0; i < 20; i += 4) {
    hash[i + 0] = metainfo->info.pieces.data[real_idx + i + 0];
    hash[i + 1] = metainfo->info.pieces.data[real_idx + i + 1];
    hash[i + 2] = metainfo->info.pieces.data[real_idx + i + 2];
    hash[i + 3] = metainfo->info.pieces.data[real_idx + i + 3];
  }
}

void torrentInfoMultiFileSet(TorrentInfo *info) {
  if (info->is_single_file) return;

  if (!info->multi_files.files) {
    usize size = sizeof(TorrentFile) * 2048;
    info->multi_files.files = malloc(size);
    assert(info->multi_files.files);
  }

  if (!info->multi_files.paths) {
    usize size = sizeof(String) * 2048;
    info->multi_files.paths = malloc(size);
    assert(info->multi_files.paths);
  }
}

TorrentMetainfo *torrentMetainfoInit() {
  TorrentMetainfo *metainfo = malloc(sizeof(TorrentMetainfo));
  memset(metainfo, 0, sizeof(TorrentMetainfo));
  metainfo->trackers_url = malloc(sizeof(String) * 2048);
  return metainfo;
}

void torrentMetainfoCleanup(TorrentMetainfo *mi) {
  assert(mi->trackers_url);
  free(mi->trackers_url);

  if (mi->info.is_single_file) {
    assert(!mi->info.multi_files.files);
    assert(!mi->info.multi_files.paths);
    return;
  }

  assert(mi->info.multi_files.files);
  free(mi->info.multi_files.files);

  assert(mi->info.multi_files.paths);
  free(mi->info.multi_files.paths);

  free(mi);
}

void torrentMetainfoPrint(TorrentMetainfo metainfo) {
  printf("meta info\n");
  printf("announce: ");
  mclPrintString(metainfo.announce);
  printf("\n");
  printf("name: ");
  mclPrintString(metainfo.info.name);
  printf("\n");
  printf("info hash: %s \n", metainfo.info_hash);
  printf("piece length: %ldK\n", metainfo.info.piece_length / 1024);
  printf("pieces: %lu\n", metainfo.info.pieces.len / 20);
  if (metainfo.info.is_single_file)
    printf("length: %ldM\n", metainfo.info.length / 1024 / 1024);

  for (u32 i = 0; i < metainfo.trackers_count; i++) {
    mclPrintString(metainfo.trackers_url[i]);
    printf("\n");
  }

  if (!metainfo.info.is_single_file) {
    if (metainfo.info.multi_files.count > 0) printf("files:\n");
    for (u32 i = 0; i < metainfo.info.multi_files.count; i++) {
      TorrentFile file = metainfo.info.multi_files.files[i];
      printf(" length: %ld\n", file.length);
      printf(" path:\n");
      for (u32 j = 0; j < file.path_count; j++) {
        String path = file.path[j];
        printf("  - %.*s\n", (u32)path.len, path.data);
      }
    }
  }
  printf("\n");
}

TorrentPeer torrentPeerGet(const char *peers, usize idx) {
  const char *entry = peers + idx * (IPV4_LEN + PORT_LEN);
  return (TorrentPeer){
      .ip = {
          .data = (const char *)entry,
          .len = IPV4_LEN,
      },
      .port = ((u8)entry[IPV4_LEN] << 8) | (u8)entry[IPV4_LEN + 1],
  };
}

TorrentPeer6 torrentPeer6Get(const char *peers, usize idx) {
  const char *entry = peers + idx * (IPV6_LEN + PORT_LEN);
  return (TorrentPeer6){
      .ip = {
          .data = (const char *)entry,
          .len = IPV6_LEN,
      },
      .port = (entry[IPV6_LEN] << 8) | entry[IPV6_LEN + 1],
  };
}
