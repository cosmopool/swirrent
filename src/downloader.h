#include "options.h"
#include "torrent.h"

typedef struct {
  char info_hash[20];
  usize pieces_bitfield;
} DownloaderProgress;

void downloaderOptionsSet(Options *);
u32 downloaderTrackerPeerListFetch(TorrentMetainfo *metainfo, TorrentTrackerResponse *out);
u32 downloaderTrackerResponseDecode(String resp, TorrentMetainfo *metainfo, TorrentTrackerResponse *out);
u32 downloaderPeerHandshake(TorrentTrackerResponse *resp, u8 *info_hash, u8 *peer_id);
