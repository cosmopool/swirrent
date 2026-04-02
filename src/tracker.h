#pragma once

#include "swirrent.h"
#include "torrent.h"

typedef struct {
  char info_hash[20];
  usize pieces_bitfield;
} DownloaderProgress;

void trackerOptionsSet(SwirrentOptions *);
u32 trackerPeerListFetch(TorrentMetainfo *metainfo, TrackerResponse *out);
u32 trackerPeer6Handshake(TrackerResponse *resp, u8 *info_hash, u8 *peer_id);
