#include "swirrent.h"
#include "torrent.h"
#include "tracker.h"

typedef struct {
  char info_hash[20];
  usize pieces_bitfield;
} DownloaderProgress;

void downloaderOptionsSet(SwirrentOptions *);
u32 downloaderTrackerPeerListFetch(TorrentMetainfo *metainfo, TrackerResponse *out);
u32 downloaderTrackerResponseDecode(String resp, TrackerResponse *out);
u32 downloaderPeerHandshake(TrackerResponse *resp, u8 *info_hash, u8 *peer_id);
