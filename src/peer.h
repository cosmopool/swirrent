#include <stdbool.h>
#include <sys/socket.h>
#ifdef __linux__
#include <endian.h>
#else
#include <sys/endian.h>
#endif

#include "core.h"
#include "torrent.h"

typedef enum {
  CHOKED,
  UNCHOKED,
  INTERESTED,
  NOT_INTERESTED,
} PEER_STATUS;

typedef struct {
  PEER_STATUS their_status;
  PEER_STATUS our_status;
  u32 bitfield;
} PeerStatus;

void peerHandshakeGenerate(u8 *info_hash, u8 *peer_id, char handshake_buff[68]);
u32 peerConnect(i32 fd, struct sockaddr *sock, usize sock_size, char *data, usize data_size);
u32 peer4Handshake(TorrentTrackerResponse *resp, u8 *info_hash, u8 *peer_id);
u32 peer6Handshake(TorrentTrackerResponse *resp, u8 *info_hash, u8 peer_id[20]);
