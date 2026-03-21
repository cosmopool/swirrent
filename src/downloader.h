#include "torrent.h"
#include "options.h"

void downloaderOptionsSet(Options *);
u32 downloaderTrackerPeerListFetch(TorrentMetainfo *);
u32 downloaderTrackerResponseDecode(String resp, TorrentMetainfo *metainfo, TorrentTrackerResponse *out);
