#include <arpa/inet.h>
#include <errno.h>
#include <sys/socket.h>
#include <unistd.h>

#include "log.h"
#include "peer.h"

u32 peerConnect(i32 fd, struct sockaddr *sock, usize sock_size, char *data, usize data_size) {
  u32 c = connect(fd, (struct sockaddr *)&sock, sock_size);
  if (c != 0) {
    logInfo("connection with peer failed: %s", strerror(errno));
    return c;
  }

  if (write(fd, data, data_size) < 0) {
    logInfo("failed to send data to peer: %s", strerror(errno));
    return -1;
  }

  char resp_buff[1024] = {0};
  if (read(fd, resp_buff, 1023) < 0) {
    logInfo("failed to read peer response: %s", strerror(errno));
    return -1;
  }
  logInfo("peer response: %s", resp_buff);

  close(fd);
  return 0;
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
  FILE *file = fopen("handshake", "wb");
  if (file) {
    // Write some text to the file
    size_t written = fwrite(handshake_buff, 1, 68, file);
    if (written < 68) {
      logInfo("Warning: Only wrote %zu of 68 bytes.", written);
    }
  } else {
    perror("fopen");
  }
  // Close the file
  if (file) fclose(file);
}

u32 peer4Handshake(TorrentTrackerResponse *resp, u8 *info_hash, u8 *peer_id) {
  char handshake_buff[68] = {0};
  peerHandshakeGenerate(info_hash, peer_id, handshake_buff);

  TorrentPeer peer = torrentPeerGet(resp->peers.data, 0);
  char ip_str[INET_ADDRSTRLEN] = {0};
  if (!inet_ntop(AF_INET, peer.ip.data, ip_str, sizeof(ip_str))) {
    logInfo("  (%d)\t failed to parse ipv: %s", 0, strerror(errno));
    return -1;
  }

  u32 result = 0;
  i32 fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    logInfo("socket error: %s", strerror(errno));
    return fd;
  }

  struct sockaddr_in sock = {
      .sin_port = htons(peer.port),
      .sin_family = AF_INET,
  };
  memcpy(&sock.sin_addr, peer.ip.data, peer.ip.len);

  u32 c = connect(fd, (struct sockaddr *)&sock, sizeof(sock));
  if (c != 0) {
    logInfo("connection with peer failed: %s", strerror(errno));
    result = c;
    goto cleanup;
  }

  if (write(fd, handshake_buff, sizeof(handshake_buff)) < 0) {
    logInfo("failed to send handshake to peer: %s", strerror(errno));
    result = -1;
    goto cleanup;
  }

  char resp_buff[1024] = {0};
  if (read(fd, resp_buff, sizeof(resp_buff) - 1) < 0) {
    logInfo("failed to read peer response: %s", strerror(errno));
    result = -1;
    goto cleanup;
  }
  logInfo("peer response: %s", resp_buff);

cleanup:
  close(fd);
  return result;
}

u32 peer6Handshake(TorrentTrackerResponse *resp, u8 *info_hash, u8 peer_id[20]) {
  char handshake_buff[68] = {0};
  peerHandshakeGenerate(info_hash, peer_id, handshake_buff);

  TorrentPeer6 peer = torrentPeer6Get(resp->peers6.data, 0);
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
      .sin_family = AF_INET,
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
