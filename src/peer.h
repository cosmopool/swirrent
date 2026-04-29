#pragma once

#include <stdbool.h>
#include <sys/socket.h>
#ifdef __linux__
#include <endian.h>
#else
#include <sys/endian.h>
#endif

#include "core.h"

typedef enum : u8 {
  CONN_NONE,
  CONN_SENT,
  CONN_CONNECTED,
} PeerConn;

typedef enum : u8 {
  STATUS_CHOKED,
  STATUS_UNCHOKED,
  STATUS_INTERESTED,
  STATUS_NOT_INTERESTED,
  STATUS_NONE,
} PeerStatus;

typedef enum : u8 {
  MESSAGE_CHOKE,
  MESSAGE_UNCHOKE,
  MESSAGE_INTERESTED,
  MESSAGE_NOT_INTERESTED,
  MESSAGE_HAVE,
  MESSAGE_BITFIELD,
  MESSAGE_REQUEST,
  MESSAGE_PIECE,
  MESSAGE_CANCEL,
} PeerMessage;

typedef struct {
  PeerStatus their_status;
  PeerStatus our_status;
  PeerConn conn_status;
  u64 bitfield;
} PeerState;

typedef struct {
  u8 length;
  u8 protocol_str[19];
  u8 reserved[8];
  u8 info_hash[SHA_DIGEST_LENGTH];
  u8 peer_id[PEER_ID_LENGTH];
} PeerHandshakeResponse;

typedef struct {
  String peer_id;
  String ip;
  u16 port;
} Peer;

void peerAdd(u8 *ip, u16 port, usize len, u8 *info_hash, u8 *peer_id);
void peerAddMany(u8 *peers, usize peer_len, usize count, u8 *info_hash, u8 *peer_id);
void peerLoop(void);
