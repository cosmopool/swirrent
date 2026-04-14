#ifdef __linux__
#include <endian.h>
#include <socket.h>
#else
#include <sys/socket.h>
#endif

#include "core.h"
#include "torrent.h"

void peerHandshakeGenerate(u8 *info_hash, u8 *peer_id, char handshake_buff[68]);
u32 peerConnect(i32 fd, struct sockaddr *sock, usize sock_size, char *data, usize data_size);
u32 peer4Handshake(TorrentTrackerResponse *resp, u8 *info_hash, u8 *peer_id);
u32 peer6Handshake(TorrentTrackerResponse *resp, u8 *info_hash, u8 peer_id[20]);
