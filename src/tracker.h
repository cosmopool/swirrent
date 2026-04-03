#pragma once

#include "swirrent.h"
#include "torrent.h"

enum TrackerAction : u32 {
  ACTION_CONNECT,
};

typedef struct {
  char info_hash[20];
  usize pieces_bitfield;
} DownloaderProgress;

void trackerOptionsSet(SwirrentOptions *);
u32 trackerPeerListFetch(TorrentMetainfo *metainfo, TorrentTrackerResponse *out);
u32 trackerPeer6Handshake(TorrentTrackerResponse *resp, u8 *info_hash, u8 *peer_id);
