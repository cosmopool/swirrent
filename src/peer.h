#pragma once

#include <stdbool.h>
#include <sys/socket.h>
#ifdef __linux__
#include <endian.h>
#else
#include <sys/endian.h>
#endif

#include "core.h"

typedef enum {
  CHOKED,
  UNCHOKED,
  INTERESTED,
  NOT_INTERESTED,
  NONE,
} PEER_STATUS;

typedef struct {
  PEER_STATUS their_status;
  PEER_STATUS our_status;
  u32 bitfield;
  struct addrinfo *addr;
  u16 port;
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

void peerHandshakeGenerate(u8 *info_hash, u8 *peer_id, char handshake_buff[68]);
i32 peerConnect(i32 fd, char *data, usize data_size);
i32 peer6Handshake(Peer, u8 *info_hash, u8 peer_id[20]);
i32 peerHandshake(void *, u16, u8 *info_hash, u8 *peer_id);

void peerAdd(u8 *peers, usize peer_size, usize peers_count);
Peer peerGet(u8 *peers, usize idx, usize len);
