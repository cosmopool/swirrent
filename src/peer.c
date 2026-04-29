#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#ifdef __linux__
#include <endian.h>
#else
#include <sys/endian.h>
#endif
#include <sys/socket.h>
#include <unistd.h>

#include "asio.h"
#include "core.h"
#include "log.h"
#include "peer.h"

typedef struct {
  u8 *info_hash;
  u8 *peer_id;
  u32 peer_idx;
  TorrentMetainfo *metainfo;
} AsioArgs;

static PeerState peers_state[MAX_FD] = {0};
static struct sockaddr_in peers_addr[MAX_FD] = {0};
static AsioArgs peers_args[MAX_FD] = {0};
static u32 peers_count = 0;

const char *peerMessageToString(PeerMessage msg) {
  switch (msg) {
  case MESSAGE_CHOKE: return "choke";
  case MESSAGE_UNCHOKE: return "unchoke";
  case MESSAGE_INTERESTED: return "interested";
  case MESSAGE_NOT_INTERESTED: return "not interested";
  case MESSAGE_HAVE: return "have";
  case MESSAGE_BITFIELD: return "bitfield";
  case MESSAGE_REQUEST: return "request";
  case MESSAGE_PIECE: return "piece";
  case MESSAGE_CANCEL: return "cancel";
  }
}

void peerRemove(u32 fd, u32 idx) {
  u32 curr = idx;
  u32 last = peers_count - 1;
  peers_addr[curr] = peers_addr[last];
  peers_addr[last] = (struct sockaddr_in){0};
  peers_state[curr] = peers_state[last];
  peers_state[last] = (PeerState){0};
  peers_args[curr] = peers_args[last];
  peers_args[curr].peer_idx = idx;
  peers_args[last] = (AsioArgs){0};
  peers_count--;
  logInfo("closing socket %d of peer %d", fd, idx);
  asioFdUnset(fd);
}

void peerRead(u32 fd, u32 idx, u8 *buff, u64 pieces_count) {
  u32 msg_offset = 0;
  u32 length = (buff[0] << 24) | (buff[1] << 16) | (buff[2] << 8) | buff[3];
  // u32 prefix = 0;
  // memcpy((u8 *)&prefix, buff, sizeof(prefix));
  // prefix = be32toh(prefix);
  msg_offset += 4;
  PeerMessage message = buff[msg_offset];
  msg_offset++;
  logInfo("(%d) message length: %d, type: %s", idx, length, peerMessageToString(message));
  switch (message) {
  case MESSAGE_CHOKE: return logInfo("choke");
  case MESSAGE_UNCHOKE: return logInfo("unchoke");
  case MESSAGE_INTERESTED: return logInfo("interested");
  case MESSAGE_NOT_INTERESTED: return logInfo("not interested");
  case MESSAGE_HAVE: return logInfo("have");
  case MESSAGE_BITFIELD: {
    break;
  }
  case MESSAGE_REQUEST: return logInfo("request");
  case MESSAGE_PIECE: return logInfo("piece");
  case MESSAGE_CANCEL: return logInfo("cancel");
  }
  return;
}

void peerListen(u32 fd, u32 idx, struct sockaddr_in peer_addr, TorrentMetainfo *metainfo) {
  logInfo("(%d) received message from peer", idx);
  u8 buff[1024] = {0};
  isize bytes_read = 0;
  if ((bytes_read = recv(fd, buff, sizeof(buff), 0)) < 0) {
    logError("(%d) failed to read peer message: %s", idx, strerror(errno));
    return peerRemove(fd, idx);
  }
  logInfo("(%d) finished reading message: %d bytes", idx, bytes_read);
  if (bytes_read == 0) return logInfo("keepalive message from peer %d", idx);
  return peerRead(fd, idx, buff, metainfo->info.pieces_count);
}

void peerHandshakeGenerate(u8 *info_hash, u8 *peer_id, char handshake_buff[68]) {
  assert(info_hash[0] != '\0');

  const char *protocol_str = "BitTorrent protocol";
  const u32 protocol_len = strlen(protocol_str);

  u32 offset = 0;
  handshake_buff[offset] = 19;
  offset++;

  memcpy(handshake_buff + offset, protocol_str, protocol_len);
  offset += protocol_len;
  assert(offset == 20);

  memset(handshake_buff + offset, 0, 8);
  offset += 8;
  assert(offset == 28);

  memcpy(handshake_buff + offset, info_hash, SHA_DIGEST_LENGTH);
  offset += SHA_DIGEST_LENGTH;
  assert(offset == 48);

  memcpy(handshake_buff + offset, peer_id, PEER_ID_LENGTH);
  offset += PEER_ID_LENGTH;
  assert(offset == 68);
}

void peerHandshakeSend(u32 fd, u32 idx, struct sockaddr_in peer_addr, TorrentMetainfo *metainfo, u8 *peer_id) {
  logInfo("(%d) connecting with peer", idx);
  u32 c = connect(fd, (struct sockaddr *)&peer_addr, sizeof(peer_addr));
  if (c != 0) {
    peerRemove(fd, idx);
    return logError("(%d) connection with peer failed: %s", idx, strerror(errno));
  }

  logInfo("(%d) generating handshake", idx);
  char handshake_buff[68] = {0};
  peerHandshakeGenerate(metainfo->info_hash, peer_id, handshake_buff);

  logInfo("(%d) sending handshake", idx);
  isize bytes_sent = 0;
  if ((bytes_sent = send(fd, handshake_buff, sizeof(handshake_buff), 0)) < 0) {
    peerRemove(fd, idx);
    return logError("(%d) failed to send handshake to peer: %s", idx, strerror(errno));
  }
  if ((usize)bytes_sent < sizeof(handshake_buff)) {
    peerRemove(fd, idx);
    return logError("(%d) failed to send whole handshake data: %s", idx, bytes_sent);
  }
  peers_state[idx].conn_status = CONN_SENT;
}

void peerHandshakeRead(u32 fd, u32 idx, struct sockaddr_in peer_addr, TorrentMetainfo *metainfo) {
  logInfo("(%d) waiting response from peer", idx);
  u8 buff[1024] = {0};
  isize bytes_read = 0;
  if ((bytes_read = recv(fd, buff, sizeof(buff), 0)) < 0) {
    logError("(%d) failed to read peer response: %s", idx, strerror(errno));
    return peerRemove(fd, idx);
  }
  logInfo("(%d) finished reading response: %d bytes", idx, bytes_read);
  if (strlen((char *)buff) == 0) {
    logError("peer closed the connection");
    return peerRemove(fd, idx);
  }
  PeerHandshakeResponse hr;
  usize offset = 0;
  hr.length = buff[offset];
  assert(hr.length == 19);
  offset += 1;
  memcpy(hr.protocol_str, buff + offset, 19);
  assert(memcmp(hr.protocol_str, "BitTorrent protocol", 19) == 0);
  offset += 19;
  memcpy(hr.reserved, buff + offset, 8);
  // assert(memcmp(hr.reserved, "", 8) == 0);
  offset += 8;
  memcpy(hr.info_hash, buff + offset, SHA_DIGEST_LENGTH);
  assert(memcmp(hr.info_hash, metainfo->info_hash, SHA_DIGEST_LENGTH) == 0);
  offset += SHA_DIGEST_LENGTH;
  memcpy(hr.peer_id, buff + offset, PEER_ID_LENGTH);
  // assert(memcmp(hr.peer_id, peer_id, PEER_ID_LENGTH) == 0);
  printf("(%d) info hash: ", idx);
  hexdump("%02x", hr.info_hash, SHA_DIGEST_LENGTH, false);
  printf("(%d)   peer id: ", idx);
  hexdump("%02x", hr.peer_id, PEER_ID_LENGTH, false);
  peers_state[idx].conn_status = CONN_CONNECTED;
  if (bytes_read <= 68) return;
  printf("(%d) full response dump: \n", idx);
  hexdump("%02X ", buff, bytes_read, true);
  return peerRead(fd, idx, buff + 68);
}

void peerResolveState(i32 fd, u64 now, ASIO_STATUS status, void *args) {
  (void)now;
  (void)status;

  AsioArgs *a = args;
  PeerState state = peers_state[a->peer_idx];
  struct sockaddr_in addr = peers_addr[a->peer_idx];
  switch (state.conn_status) {
  case CONN_NONE: return peerHandshakeSend(fd, a->peer_idx, addr, a->metainfo, a->peer_id);
  case CONN_SENT: return peerHandshakeRead(fd, a->peer_idx, addr, a->metainfo);
  case CONN_CONNECTED: return peerListen(fd, a->peer_idx, addr, a->metainfo);
  }
}

void peerAdd(u8 *ip, u16 port, usize len, TorrentMetainfo *metainfo, u8 *peer_id) {
  assert(len == IPV4_LEN || len == IPV6_LEN);
  logInfo("[PEER] adding peer %d", peers_count);

  u32 af = 0;
  u32 addr_str_len = 0;
  switch (len) {
  case IPV4_LEN:
    af = AF_INET;
    addr_str_len = INET_ADDRSTRLEN;
    break;
  case IPV6_LEN:
    af = AF_INET6;
    addr_str_len = INET6_ADDRSTRLEN;
    break;
  default:
    return logError("\t invalid peer length: %d. only ipv4 & ipv6 lengths are valid.", len);
  }

  i32 fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return logError("failed opening socket", strerror(errno));
  struct sockaddr_in addr = {
      .sin_port = htobe16(port),
      .sin_family = af,
  };
  memcpy(&addr.sin_addr, ip, len);

  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) {
    return logError("\terror fetching current time: %s", strerror(errno));
  }
  char buf[addr_str_len];
  if (!inet_ntop(af, &addr.sin_addr, buf, sizeof(buf))) {
    return logError("\t failed to parse ipv4: %s", strerror(errno));
  }
  logInfo("(%d) ip: %s\t | port: %d", peers_count, buf, be16toh(addr.sin_port));
  peers_addr[peers_count] = addr;
  peers_state[peers_count] = (PeerState){.our_status = STATUS_NONE, .their_status = STATUS_NONE};
  peers_args[peers_count] = (AsioArgs){.peer_idx = peers_count, .metainfo = metainfo, .peer_id = peer_id};
  u32 idx = peers_count;
  peers_count++;
  asioFdSet((AsioFd){.fd = fd, .args = peers_args + idx, .on_ready_callback = peerResolveState});
  peerResolveState(fd, ts.tv_sec, ASIO_NONE, peers_args + idx);
}

void peerAddMany(u8 *peers, usize peer_len, usize count, TorrentMetainfo *metainfo, u8 *peer_id) {
  logInfo("[PEER] adding %d peers: ", count);
  for (u32 i = 0; i < count; i++) {
    u8 *ip_offset = peers + i * (peer_len + PORT_LEN);
    u16 port = ((u8)ip_offset[peer_len] << 8) | (u8)ip_offset[peer_len + 1];
    peerAdd(ip_offset, port, peer_len, metainfo, peer_id);
  }
  logInfo("");
}
