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

typedef struct {
  u8 length;
  u8 protocol_str[19];
  u8 reserved[8];
  u8 info_hash[SHA_DIGEST_LENGTH];
  u8 peer_id[PEER_ID_LENGTH];
} PeerHandshakeResponse;

void peerHandshakeGenerate(u8 *info_hash, u8 *peer_id, char handshake_buff[68]);
i32 peerConnect(i32 fd, struct sockaddr *sock, usize sock_size, char *data, usize data_size);
i32 peer4Handshake(TorrentPeer, u8 *info_hash, u8 *peer_id);
i32 peer6Handshake(TorrentPeer6, u8 *info_hash, u8 peer_id[20]);
i32 peerHandshake(void *, u16, u8 *info_hash, u8 *peer_id);
