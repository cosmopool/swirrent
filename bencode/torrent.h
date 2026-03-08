#ifndef _TORRENT_DEF
#define _TORRENT_DEF

#include "core.h"
#include <stdbool.h>

typedef struct {
  String *path; // Array of path components
  usize length;
  usize path_count;
} TorrentFile;

typedef struct {
  // The URL of the tracker.
  String announce;

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

  // Discriminator: true for single file, false for multi-file
  bool is_single_file;

  union {
    struct {
      // length - The length of the file, in bytes.
      usize length;
    } single_file;

    struct {
      // files list - array of file dictionaries
      TorrentFile *files;
      usize file_count;
    } multi_file;
  };

} TorrentMetainfo;

void Torrent_GetPieceHash(usize piece_idx, TorrentMetainfo *metainfo,
                          char *hash);

typedef enum {
  TRACKER_EVENT_NONE,
  TRACKER_EVENT_STARTED,
  TRACKER_EVENT_COMPLETED,
  TRACKER_EVENT_STOPPED,
} TrackerEvent;

typedef struct {
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

typedef struct {
  // Tracker response fields
  // failure reason - optional human readable string explaining why the query
  // failed
  String failure_reason;

  // interval - number of seconds the downloader should wait between regular
  // rerequests
  usize interval;

  // peers - list of dictionaries corresponding to peers
  struct {
    String peer_id;
    String ip;
    u16 port;
  } *peers;
  usize peer_count;
} asdf;

#ifndef TORRENT_IMPLEMENTATION
#define TORRENT_IMPLEMENTATION

void Torrent_GetPieceHash(usize piece_idx, TorrentMetainfo *metainfo,
                          char *hash) {
  usize real_idx = piece_idx * 20;
  for (u32 i = 0; i < 20; i += 4) {
    hash[i + 0] = metainfo->pieces.data[real_idx + i + 0];
    hash[i + 1] = metainfo->pieces.data[real_idx + i + 1];
    hash[i + 2] = metainfo->pieces.data[real_idx + i + 2];
    hash[i + 3] = metainfo->pieces.data[real_idx + i + 3];
  }
}

#endif // TORRENT_IMPLEMENTATION

#endif // !_TORRENT_DEF
