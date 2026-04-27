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
  PEER_CONN_NONE,
  PEER_CONN_SENT,
  PEER_CONN_CONNECTED,
} PEER_CONN;

typedef enum : u8 {
  CHOKED,
  UNCHOKED,
  INTERESTED,
  NOT_INTERESTED,
  NONE,
} PEER_STATUS;

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
} PEER_MESSAGE;

typedef struct {
  PEER_STATUS their_status;
  PEER_STATUS our_status;
  PEER_CONN conn_status;
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
