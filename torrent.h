#ifndef _TORRENT_DEF
#define _TORRENT_DEF

#include "core.h"
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef struct TorrentFile {
  usize length;
  usize path_count;
  String *path; // Array of path components
} TorrentFile;

typedef struct TorrentInfoFiles {
  // files list - array of file dictionaries
  TorrentFile *files;
  String *paths;
  usize count;
} TorrentInfoFiles;

typedef struct TorrentInfo {
  // Discriminator: true for single file, false for multi-file
  bool is_single_file;

  // The name key maps to a UTF-8 encoded string which is the suggested name to
  // save the file (or directory) as. It is purely advisory.
  String name;

  // piece length maps to the number of bytes in each piece the file is split
  // into. For the purposes of transfer, files are split into fixed-size pieces
  // which are all the same length except for possibly the last one which may be
  // truncated. piece length is almost always a power of two, most commonly 2 18
  // = 256 K (BitTorrent prior to version 3.2 uses 2 20 = 1 M as default).
  usize piece_length;

  // pieces maps to a string whose length is a multiple of 20. It is to be
  // subdivided into strings of length 20, each of which is the SHA1 hash of the
  // piece at the corresponding index.
  String pieces;

  union {
    // length - The length of the file, in bytes.
    usize length;

    TorrentInfoFiles multi_files;
  };
} TorrentInfo;

typedef struct TorrentMetainfo {
  // The URL of the tracker.
  String announce;
  String *trackers_url;
  usize trackers_count;

  char info_hash[20 * 2 + 1];

  TorrentInfo info;
} TorrentMetainfo;

void torrentPieceHashGet(usize piece_idx, TorrentMetainfo *metainfo,
                         char *hash);

typedef enum {
  TRACKER_EVENT_NONE,
  TRACKER_EVENT_STARTED,
  TRACKER_EVENT_COMPLETED,
  TRACKER_EVENT_STOPPED,
} TrackerEvent;

typedef struct TorrentTracker {
  // info_hash
  // The 20 byte sha1 hash of the bencoded form of the info value from the
  // metainfo file. This value will almost certainly have to be escaped.
  unsigned char info_hash[20];

  // peer_id
  // A string of length 20 which this downloader uses as its id.
  // Each downloader generates its own id at random at the start
  // of a new download. This value will also almost certainly
  // have to be escaped.
  unsigned char peer_id[20];

  // ip
  // An optional parameter giving the IP (or dns name)
  // which this peer is at. Generally used for the
  // origin if it's on the same machine as the tracker.
  String ip;

  // port
  // The port number this peer is listening on. Common behavior is for a
  // downloader to try to listen on port 6881 and if that port is taken
  // try 6882, then 6883, etc. and give up after 6889.
  u16 port;

  // uploaded
  // The total amount uploaded so far, encoded in base ten ascii.
  usize uploaded;

  // downloaded
  // The total amount downloaded so far, encoded in base ten ascii.
  usize downloaded;

  // left
  // The number of bytes this peer still has to download, encoded in
  // base ten ascii. Note that this can't be computed from downloaded
  // and the file length since it might be a resume, and there's a
  // chance that some of the downloaded data failed an integrity check
  // and had to be re-downloaded.
  usize left;

  // event
  // This is an optional key which maps to started, completed, or stopped (or
  // empty, which is the same as not being present). If not present, this is one
  // of the announcements done at regular intervals. An announcement using
  // started is sent when a download first begins, and one using completed is
  // sent when the download is complete. No completed is sent if the file was
  // complete when started. Downloaders send an announcement using stopped when
  // they cease downloading.
  TrackerEvent event;
} TorrentTracker;

typedef struct TorrentPeer {
  String peer_id;
  u32 ip;
  u16 port;
} TorrentPeer;

typedef struct TorrentTrackerResponse {
  // Tracker response fields
  // failure reason - optional human readable string explaining why the query
  // failed
  String failure_reason;
  String warning_message;

  // interval - number of seconds the downloader should wait between regular
  // rerequests
  usize interval;
  usize min_interval;

  usize complete;
  usize downloaded;
  usize incomplete;

  // peers - list of dictionaries corresponding to peers
  TorrentPeer *peers;
  usize peer_count;
} TorrentTrackerResponse;

#ifndef TORRENT_IMPLEMENTATION
#define TORRENT_IMPLEMENTATION

void torrentPieceHashGet(usize piece_idx, TorrentMetainfo *metainfo,
                         char *hash) {
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
    // memset(info->multi_files.files, 0, size);
  }

  if (!info->multi_files.paths) {
    usize size = sizeof(String) * 2048;
    info->multi_files.paths = malloc(size);
    assert(info->multi_files.paths);
    // memset(info->multi_files.files, 0, size);
  }
}

void torrentMetainfoCleanup(TorrentMetainfo *mi) {
  if (mi->info.is_single_file) {
    assert(!mi->info.multi_files.files);
    assert(!mi->info.multi_files.paths);
    return;
  }

  assert(mi->info.multi_files.files);
  free(mi->info.multi_files.files);

  assert(mi->info.multi_files.paths);
  free(mi->info.multi_files.paths);
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

#endif // TORRENT_IMPLEMENTATION

#endif // !_TORRENT_DEF
