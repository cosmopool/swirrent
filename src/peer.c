#include <arpa/inet.h>
#include <errno.h>
#include <sys/socket.h>
#include <unistd.h>

#include "core.h"
#include "log.h"
#include "peer.h"

// static PeerState peers[MAX_FD] = {0};
// static i32 fd_to_peer_idx[MAX_FD] = {0};

void peerAdd(u8 *peers, usize peer_len, usize peers_count) {
  logInfo("[PEER] add:");
  for (u32 i = 0; i < peers_count; i++) {
    Peer peer = peerGet((u8 *)peers, i, peer_len);
    char buf[INET_ADDRSTRLEN] = {0};
    if (!inet_ntop(AF_INET, peer.ip.data, buf, sizeof(buf))) {
      return logError("\t failed to parse ipv4: %s\n", i, strerror(errno));
    }
    logInfo("\t ip: %s\t | port: %d", buf, peer.port);
  }
  logInfo("");
}

Peer peerGet(u8 *peers, usize idx, usize len) {
  u8 *entry = peers + idx * (len + PORT_LEN);
  return (Peer){
      .ip = {
          .data = (const char *)entry,
          .len = len,
      },
      .port = ((u8)entry[len] << 8) | (u8)entry[len + 1],
  };
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

// i32 peerConnect(i32 fd, char *data, usize data_size) {
//   i32 c = connect(fd, (struct sockaddr *)&sock, sock_size);
//   if (c != 0) {
//     logError("connection with peer failed: %s", strerror(errno));
//     return c;
//   }
//
//   if (send(fd, data, data_size, 0) < 0) {
//     logError("failed to send data to peer: %s", strerror(errno));
//     return -1;
//   }
//
//   char resp_buff[1024] = {0};
//   if (recv(fd, resp_buff, 1024, 0) < 0) {
//     logError("failed to read peer response: %s", strerror(errno));
//     return -1;
//   }
//   logInfo("peer response: %s", resp_buff);
//
//   close(fd);
//   return 0;
// }

i32 peerHandshake(void *ip, u16 port, u8 *info_hash, u8 *peer_id) {
  u32 result = 0;
  i32 fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    logError("failed opening socket", strerror(errno));
    return fd;
  }

  struct sockaddr_in peer_addr = {
      .sin_port = htobe16(port),
      .sin_family = AF_INET,
  };
  i32 r = inet_pton(AF_INET, ip, &(peer_addr.sin_addr));
  if (r == 0) {
    logError("invalid ip format: %s", ip);
    result = r;
    goto cleanup;
  }
  if (r < 0) {
    logError("invalid ip: %s", strerror(errno));
    result = r;
    goto cleanup;
  }

  logInfo("connecting with peer");
  u32 c = connect(fd, (struct sockaddr *)&peer_addr, sizeof(peer_addr));
  if (c != 0) {
    logError("connection with peer failed: %s", strerror(errno));
    result = c;
    goto cleanup;
  }

  logInfo("generating handshake");
  char handshake_buff[68] = {0};
  peerHandshakeGenerate(info_hash, peer_id, handshake_buff);

  logInfo("sending handshake");
  isize bytes_sent = 0;
  if ((bytes_sent = send(fd, handshake_buff, sizeof(handshake_buff), 0)) < 0) {
    logError("failed to send handshake to peer: %s", strerror(errno));
    result = -1;
    goto cleanup;
  }
  if ((usize)bytes_sent < sizeof(handshake_buff)) {
    logError("failed to send whole handshake data: %s", bytes_sent);
    result = -1;
    goto cleanup;
  }

  logInfo("waiting response from peer");
  u8 buff[1024] = {0};
  isize bytes_read = 0;
  if ((bytes_read = recv(fd, buff, sizeof(buff), 0)) < 0) {
    logError("failed to read peer response: %s", strerror(errno));
    result = -1;
    goto cleanup;
  }
  logInfo("finished reading response: %d bytes", bytes_read);
  if (strlen((char *)buff) == 0) {
    logError("peer closed the connection");
    result = -1;
    goto cleanup;
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
  assert(memcmp(hr.info_hash, info_hash, SHA_DIGEST_LENGTH) == 0);
  offset += SHA_DIGEST_LENGTH;
  memcpy(hr.peer_id, buff + offset, PEER_ID_LENGTH);
  // assert(memcmp(hr.peer_id, peer_id, PEER_ID_LENGTH) == 0);
  logInfo("\tinfo hash: 0x%02X", hr.info_hash);
  logInfo("\t  peer id: 0x%02X", hr.peer_id);

cleanup:
  logInfo("closing socket (%d)", fd);
  close(fd);
  return result;
}

i32 peer6Handshake(Peer peer, u8 *info_hash, u8 peer_id[20]) {
  char handshake_buff[68] = {0};
  peerHandshakeGenerate(info_hash, peer_id, handshake_buff);

  // TorrentPeer6 peer = torrentPeer6Get(resp->peers6.data, 0);
  char ip_str[INET6_ADDRSTRLEN] = {0};
  if (!inet_ntop(AF_INET6, peer.ip.data, ip_str, sizeof(ip_str))) {
    logInfo("  (%d)\t failed to parse ipv6: %s", 0, strerror(errno));
    return -1;
  }

  u32 result = 0;
  i32 fd = socket(AF_INET6, SOCK_STREAM, 0);
  if (fd < 0) {
    logInfo("socket error: %s", strerror(errno));
    return fd;
  }

  struct sockaddr_in sock = {
      .sin_port = htons(peer.port),
      .sin_family = AF_INET6,
  };
  memcpy(&sock.sin_addr, peer.ip.data, peer.ip.len);

  u32 c = connect(fd, (struct sockaddr *)&sock, sizeof(sock));
  if (c != 0) {
    logInfo("connection with peer failed: %s", strerror(errno));
    return c;
  }

  if (send(fd, handshake_buff, sizeof(handshake_buff), 0) < 0) {
    logInfo("failed to send handshake to peer: %s", strerror(errno));
    return -1;
  }

  char resp_buff[1024] = {0};
  if (recv(fd, resp_buff, sizeof(resp_buff) - 1, 0) < 0) {
    logInfo("failed to read peer response: %s", strerror(errno));
    return -1;
  }
  logInfo("peer response: %s", resp_buff);

  close(fd);
  return result;
}
