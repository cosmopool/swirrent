#pragma once

#include <netdb.h>
#include <sys/socket.h>

#include "core.h"

#define ANNOUNCE_SIZE 98
#define CONNECT_REQUEST_SIZE 16

typedef enum : u32 {
  ACTION_CONNECT,
  ACTION_ANNOUNCE,
  ACTION_NONE,
} TrackerAction;

typedef enum : u32 {
  TRACKER_EVENT_NONE,
  TRACKER_EVENT_STARTED,
  TRACKER_EVENT_COMPLETED,
  TRACKER_EVENT_STOPPED,
} TrackerEvent;

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

typedef struct {
  u8 *info_hash;
  u8 *peer_id;
  void (*add_peer_callback)(u8 *peers, usize peer_size, usize peers_count);
} AsioArgs;

typedef struct {
  u32 id;
  usize last_try;
  u64 connection_id;
  u64 transaction_id;
  TrackerEvent event;
  TrackerAction action;
  u8 tries;
  u16 port;
  struct addrinfo *addr;
  String url;
} TrackerState;

typedef struct {
  u64 connection_id;

  // info_hash
  // The 20 byte sha1 hash of the bencoded form of the info value from the
  // metainfo file. This value will almost certainly have to be escaped.
  u8 info_hash[20];

  // peer_id
  // A string of length 20 which this downloader uses as its id.
  // Each downloader generates its own id at random at the start
  // of a new download. This value will also almost certainly
  // have to be escaped.
  u8 peer_id[20];

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
} Tracker;

u32 trackerPeerListFetch(String *urls, usize count, u8 info_hash[SHA_DIGEST_LENGTH], u8 peer_id[PEER_ID_LENGTH],
                         void (*add_peer_callback)(u8 *peers, usize peer_size, usize peers_count));
