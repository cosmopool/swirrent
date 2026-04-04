#pragma once

#include "swirrent.h"
#include "torrent.h"

#define ANNOUNCE_SIZE 98
#define CONNECT_REQUEST_SIZE 16

typedef enum : u32 {
  ACTION_CONNECT,
  ACTION_ANNOUNCE,
} TrackerAction;

typedef struct {
  char info_hash[20];
  usize pieces_bitfield;
} DownloaderProgress;

typedef struct {
  u64 connection_id;
  u32 action;
  u32 transaction_id;
  u8 info_hash[20];
  u8 peer_id[20];
  u64 downloaded;
  u64 left;
  u64 uploaded;
  u32 event;
  u32 ip;
  u32 key;
  i32 num_want;
  u16 port;
} __attribute__((packed)) TrackerAnnounceRequest;

typedef struct {
  u64 protocol_id;
  TrackerAction action;
  u32 transaction_id;
} __attribute__((packed)) TrackerConnectRequest;

typedef struct {
  TrackerAction action;
  u32 transaction_id;
  u64 connection_id;
} __attribute__((packed)) TrackerConnectResponse;

// Values are in network-byte order
typedef struct {
  TrackerAction action;
  u32 transaction_id;
  u32 interval;
  u32 leechers;
  u32 seeders;
  u8 peers[];
} __attribute__((packed)) TrackerAnnounceResponse;

void trackerOptionsSet(SwirrentOptions *);
u32 trackerPeerListFetch(TorrentMetainfo *metainfo, TorrentTrackerResponse *out, u8 peer_id[20]);
u32 trackerPeer6Handshake(TorrentTrackerResponse *resp, u8 *info_hash, u8 peer_id[20]);
