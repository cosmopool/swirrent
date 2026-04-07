#include <arpa/inet.h>
#include <assert.h>
#include <curl/curl.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/endian.h>
#include <sys/types.h>
#include <sys/unistd.h>
#include <unistd.h>

#define STRING_IMPLEMENTATION
#include "core.h"
#include "torrent.h"
#include "tracker.h"

#define FD_SIZE 400
#define PORT "6666"

static char data[1024 * 1024] = {0};
static SwirrentOptions *options = {0};

void pfdsAddTo(struct pollfd *pfds, i32 new_fd, u32 *fd_count) {
  assert(*fd_count < FD_SIZE);
  // If we don't have room, add more space in the pfds array
  // if (*fd_count == *fd_size) {
  //   *fd_size *= 2;
  //   *pfds = realloc(*pfds, sizeof(**pfds) * (*fd_size));
  // }
  pfds[*fd_count].fd = new_fd;
  pfds[*fd_count].events = POLLIN; // Check ready-to-read
  pfds[*fd_count].revents = 0;
  (*fd_count)++;
}

void pfdsDeleteFrom(struct pollfd pfds[], TrackerPollContext ctx[], u32 i, u32 *fd_count) {
  // Copy the one from the end over this one
  pfds[i] = pfds[*fd_count - 1];
  ctx[i] = ctx[*fd_count - 1];
  (*fd_count)--;
}

// simple write callback
usize write_cb(char *ptr, usize size, usize nmemb, void *userdata) {
  if (options->verbose) printf("----- RESPONSE SIZE: %ld\n", size * nmemb);
  if (options->verbose) printf("----- RESPONSE DATA: %s\n", ptr);
  if (options->dump_response) {
    FILE *file = fopen(options->raw_request_output_path, "wb");
    if (file) {
      // Write some text to the file
      size_t written = fwrite(ptr, 1, size * nmemb, file);
      if (written < size * nmemb) {
        printf("Warning: Only wrote %zu of %zu bytes.\n", written, size * nmemb);
      }
    } else {
      perror("fopen");
    }
    // Close the file
    fclose(file);
  }

  String *r = (String *)userdata;
  r->len += size * nmemb;
  memcpy((void *)r->data, ptr, r->len);
  return size * nmemb;
}

void trackerOptionsSet(SwirrentOptions *op) {
  options = op;
}

TorrentTrackerResponse trackerResponseDecode(String resp) {
  TorrentTrackerResponse t_resp = {0};
  torrentResponseDecode(&resp, &t_resp);

  if (t_resp.warning_message.len > 0 && t_resp.peers.len == 0) {
    printf("----- Skipping tracker with warning_message: %.*s. Trying another one.\n",
           (u32)t_resp.warning_message.len, t_resp.warning_message.data);
    return t_resp;
  }
  if (t_resp.failure_reason.len > 0 && t_resp.peers.len == 0) {
    printf("----- Skipping tracker because failed: %.*s. Trying another one.\n",
           (u32)t_resp.failure_reason.len, t_resp.failure_reason.data);
    return t_resp;
  }
  printf("\n===| TRACKER RESPONSE\n");
  printf("interval: %ld\n", t_resp.interval);
  printf("min interval: %ld\n", t_resp.min_interval);
  printf("complete: %ld\n", t_resp.complete);
  printf("incomplete: %ld\n", t_resp.incomplete);
  printf("downloaded: %ld\n", t_resp.downloaded);
  printf("warning_message: %.*s\n", (u32)t_resp.warning_message.len, t_resp.warning_message.data);
  printf("failure_reason: %.*s\n", (u32)t_resp.failure_reason.len, t_resp.failure_reason.data);
  for (u32 i = 0; i < t_resp.peers6.count; i++) {
    if (i == 0) printf("peers6:\n");
    TorrentPeer6 peer = torrentPeer6Get(t_resp.peers6.data, i);
    char ip_buff[INET6_ADDRSTRLEN] = {0};
    if (!inet_ntop(AF_INET6, peer.ip.data, ip_buff, sizeof(ip_buff))) {
      printf("  (%d)\t failed to parse ipv6: %s\n", i, strerror(errno));
      continue;
    }
    printf("  (%d)\t ip: %s \t | port: %d\n", i, ip_buff, peer.port);
  }

  for (u32 i = 0; i < t_resp.peers.len / (IPV4_LEN + PORT_LEN); i++) {
    if (i == 0) printf("peers:\n");
    TorrentPeer peer = torrentPeerGet(t_resp.peers.data, i);
    char ip_buff[INET_ADDRSTRLEN] = {0};
    if (!inet_ntop(AF_INET, peer.ip.data, ip_buff, sizeof(ip_buff))) {
      printf("  (%d)\t failed to parse ipv4: %s\n", i, strerror(errno));
      continue;
    }
    printf("  (%d)\t ip: %s \t | port: %d\n", i, ip_buff, peer.port);
  }
  return t_resp;
}

void parse_tracker_url(String url, char *host, size_t host_len, char *port, size_t port_len) {
  // Skip "udp://" or "http://"
  const char *p = strstr(url.data, "://");
  if (p)
    p += 3;
  else
    p = url.data;

  // Find end of host (':' for port, '/' for path)
  const char *end = p;
  while (*end && *end != ':' && *end != '/') end++;

  // Extract host
  size_t host_size = end - p;
  if (host_size >= host_len) host_size = host_len - 1;
  strncpy(host, p, host_size);
  host[host_size] = '\0';

  // Extract port if present
  if (*end == ':') {
    const char *port_start = end + 1;
    const char *port_end = strchr(port_start, '/');
    size_t port_size = port_end ? port_end - port_start : strlen(port_start);
    if (port_size >= port_len) port_size = port_len - 1;
    strncpy(port, port_start, port_size);
    port[port_size] = '\0';
  } else {
    strcpy(port, "80"); // default
  }
}

i32 trackerAnnounceStart(u8 info_hash[20], u32 i, struct pollfd *pfds, TrackerPollContext *trackerpfds, u8 peer_id[20]) {
  TorrentTracker tracker = {
      .connection_id = trackerpfds[i].connection_id,
      .event = TRACKER_EVENT_NONE,
      .port = trackerpfds[i].port,
  };

  trackerpfds[i].action = ACTION_ANNOUNCE;
  TrackerAnnounceRequest request = {
      .connection_id = htobe64(trackerpfds[i].connection_id),
      .action = htobe32(trackerpfds[i].action),
      .transaction_id = htobe32((u32)rand()),
      .downloaded = htobe64(tracker.downloaded),
      .left = htobe64(tracker.left),
      .uploaded = htobe64(tracker.uploaded),
      .event = htobe32(TRACKER_EVENT_NONE),
      .ip = htobe32(0),
      .key = htobe32(0),
      .num_want = htobe32(-1),
      .port = htobe16(trackerpfds[i].port),
  };
  memcpy(request.info_hash, info_hash, 20);
  memcpy(request.peer_id, peer_id, 20);

  if (sendto(pfds[i].fd, &request, ANNOUNCE_SIZE, 0, trackerpfds[i].addr->ai_addr, trackerpfds[i].addr->ai_addrlen) < 0) {
    fprintf(stderr, "\tfailed to send announce to tracker: %s\n", strerror(errno));
    return -1;
  }

  return 0;
}

i32 trackerAnnounceFinish(u32 i, struct pollfd *pfds, TrackerPollContext *trackerpfds) {
  u8 buff[2048] = {0};
  TrackerAnnounceResponse *response = (TrackerAnnounceResponse *)buff;
  // TrackerAnnounceResponse *response = malloc(2048);
  // bzero(response, 2048);

  isize bytes_read = recvfrom(pfds[i].fd, response, sizeof(*response), MSG_WAITALL, &trackerpfds[i].from, &trackerpfds[i].from_len);
  if (bytes_read == 0) {
    fprintf(stderr, "\ttracker(%d) announce response: tracker has closed the connection: %s\n", i, strerror(errno));
    return -1;
  }
  if (bytes_read < 0) {
    // these errors are timeouts
    assert(errno != EAGAIN && errno != EWOULDBLOCK);
    fprintf(stderr, "\ttracker(%d) announce response: failed to read tracker response: %s\n", i, strerror(errno));
    return -1;
  }
  const isize valid_rc = 20;
  if (bytes_read < valid_rc) {
    fprintf(stderr, "\ttracker(%d) announce response: invalid tracker connect response: wrong length (%ld)\n", i, bytes_read);
    return -1;
  }

  if (be32toh(response->action) != ACTION_ANNOUNCE) {
    fprintf(stderr, "\ttracker(%d) announce response: invalid tracker announce response: wrong action\n", i);
    return -1;
  }
  if (be32toh(response->transaction_id) != trackerpfds[i].transaction_id) {
    fprintf(stderr, "\ttracker(%d) announce response: invalid tracker announce response: wrong transaction id\n", i);
    return -1;
  }
  printf("\ttracker(%d): interval: %u\n", i, be32toh(response->interval));
  printf("\ttracker(%d): leechers: %u\n", i, be32toh(response->leechers));
  printf("\ttracker(%d): seeders: %u\n", i, be32toh(response->seeders));
  printf("\ttracker(%d): peers %lu:\n", i, (20 - sizeof(*response)) / 6);
  for (u32 j = 0; j < (20 - sizeof(*response)) / 6; j++) {
    TorrentPeer peer = torrentPeerGet((char *)response->peers, j);
    char buf[INET_ADDRSTRLEN] = {0};
    if (!inet_ntop(AF_INET, peer.ip.data, buf, sizeof(buf))) {
      fprintf(stderr, "\t(%d)\t failed to parse ipv4: %s\n", j, strerror(errno));
      return -1;
    }
    printf("\t(%d)\t ip: %s\t | port: %d\n", j, buf, peer.port);
  }
  printf("\n");

  return 0;
  // cleanup:
  //   if (response != NULL) free(response);
  //   return -1;
}

i32 trackerConnectionStart(u32 i, String tracker_url, struct pollfd *pfds, u32 *fd_count, TrackerPollContext *trackerpfd) {
  assert(tracker_url.data[0] == 'u');
  assert(tracker_url.data[1] == 'd');
  assert(tracker_url.data[2] == 'p');

  TrackerPollContext ctx = {.url = tracker_url, .idx = i};
  struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_DGRAM};
  // resolve tracker ip
  char host[256], port[16];
  parse_tracker_url(tracker_url, host, sizeof(host), port, sizeof(port));
  i32 get_addr_status;
  if ((get_addr_status = getaddrinfo(host, port, &hints, &ctx.addr)) != 0) {
    fprintf(stderr, "\tgetaddrinfo: %s\n", gai_strerror(get_addr_status));
    return -1;
  }
  // print ipv4 of tracker
  struct sockaddr_in *ipv4 = (struct sockaddr_in *)(void *)ctx.addr->ai_addr;
  char ip[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &ipv4->sin_addr, ip, sizeof(ip));
  printf("\topening socket for: %s:%d\n", ip, ntohs(ipv4->sin_port));
  // nintendo switch only supports ipv4
  assert(ctx.addr->ai_family == AF_INET);
  assert(ctx.addr->ai_socktype == SOCK_DGRAM);

  // opening socket
  i32 fd = socket(ctx.addr->ai_family, ctx.addr->ai_socktype, ctx.addr->ai_protocol);
  if (fd < 0) {
    fprintf(stderr, "\tsocket error: %s\n", strerror(errno));
    return -1;
  }
  pfdsAddTo(pfds, fd, fd_count);

  // bind to any port
  struct sockaddr_in src = {
      .sin_family = AF_INET,
      .sin_addr.s_addr = htonl(INADDR_ANY),
      .sin_port = htons(0),
  };
  if (bind(fd, (struct sockaddr *)&src, sizeof(src)) < 0) {
    fprintf(stderr, "\tfailed to bind to tracker fd: %s\n", strerror(errno));
    pfdsDeleteFrom(pfds, trackerpfd, *fd_count - 1, fd_count);
    return -1;
  }

  // send connect request
  u64 protocol_fixed_value = 0x41727101980;
  trackerpfd->action = ACTION_CONNECT;
  TrackerConnectRequest req = {
      .protocol_id = htobe64(protocol_fixed_value),
      .action = htobe32(trackerpfd->action),
      .transaction_id = htobe32((u32)rand()),
  };
  assert(sizeof(req) == 16);

  printf("\t--- sending connect request to tracker\n");
  if (sendto(fd, &req, CONNECT_REQUEST_SIZE, 0, ctx.addr->ai_addr, ctx.addr->ai_addrlen) < 0) {
    fprintf(stderr, "\tfailed to send connect request to tracker: %s\n", strerror(errno));
    pfdsDeleteFrom(pfds, trackerpfd, *fd_count - 1, fd_count);
    return -1;
  }

  ctx.transaction_id = be32toh(req.transaction_id);
  ctx.action = be32toh(req.action);
  ctx.port = be16toh(ipv4->sin_port);
  ctx.status = STATUS_SENT;
  // fd_count - 1: because pfdsAddTo always leave the fd_count pointing to an empty place
  trackerpfd[*fd_count - 1] = ctx;
  return 0;
}

i32 trackerConnectionFinish(struct pollfd *fds, u32 i, TrackerPollContext *trackerpfd) {
  TrackerConnectResponse *response = malloc(1024);
  isize bytes_read = recvfrom(fds[i].fd, response, sizeof(*response), MSG_WAITALL, &trackerpfd->from, &trackerpfd->from_len);
  if (bytes_read == 0) {
    fprintf(stderr, "\ttracker (%d) connect response: connection was closed by tracker: %s\n", i, strerror(errno));
    return -1;
  }
  if (bytes_read < 0) {
    // these errors are timeouts
    assert(errno != EAGAIN || errno != EWOULDBLOCK);
    fprintf(stderr, "\ttracker (%d) connect response: failed to read response: %s\n", i, strerror(errno));
    return -1;
  }
  const isize valid_length = 16;
  if (bytes_read < valid_length) {
    fprintf(stderr, "\ttracker (%d) connect response: wrong length (%ld)\n", i, bytes_read);
    return -1;
  }
  // check tracker connect response
  if (be32toh(response->transaction_id) != trackerpfd[i].transaction_id) {
    fprintf(stderr, "\ttracker (%d) connect response: invalid transaction_id in response!\n", i);
    return -1;
  }
  if (be32toh(response->action) != trackerpfd[i].action) {
    fprintf(stderr, "\ttracker (%d) connect response: action in response!\n", i);
    return -1;
  }
  trackerpfd[i].connection_id = be64toh(response->connection_id);
  printf("\ttracker (%d): successfully connected with id: %llu\n", i, trackerpfd[i].connection_id);
  return 0;
}

TorrentTrackerResponse trackerHttpFetch(CURL *curl, String tracker_url, TorrentMetainfo *metainfo, u8 *peer_id) {
  assert(tracker_url.data[0] == 'h');
  assert(tracker_url.data[1] == 't');
  assert(tracker_url.data[2] == 't');
  assert(tracker_url.data[3] == 'p');

  curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

  usize offset = 0;
  char url[1024] = {0};
  offset += snprintf(url, tracker_url.len + 1, "%.*s", (i32)tracker_url.len, tracker_url.data);
  switch (url[tracker_url.len - 1]) {
  case '/':
    assert(url[tracker_url.len] == '\0');
    url[tracker_url.len - 1] = '?';
    break;

  default:
    assert(url[tracker_url.len] == '\0');
    url[tracker_url.len] = '?';
    break;
  }

  usize hash_offset = 0;
  char encoded_hash[61] = {0};
  for (int i = 0; i < SHA_DIGEST_LENGTH; ++i) {
    hash_offset += sprintf(encoded_hash + hash_offset, "%%%02x", (u8)metainfo->info_hash[i]);
  }
  offset += snprintf(url + offset, 1024 - offset, "info_hash=%s", encoded_hash);
  offset += snprintf(url + offset, 1024 - offset, "&peer_id=%s", peer_id);
  offset += snprintf(url + offset, 1024 - offset, "&port=%s", "6881");
  offset += snprintf(url + offset, 1024 - offset, "&uploaded=%s", "0");
  offset += snprintf(url + offset, 1024 - offset, "&downloaded=%s", "0");
  if (metainfo->info.is_single_file) {
    offset += snprintf(url + offset, 1024 - offset, "&left=%ld", metainfo->info.length);
  } else {
    usize len = 0;
    for (u32 i = 0; i < metainfo->info.multi_files.count; i++) {
      len += metainfo->info.multi_files.files[i].length;
    }
    offset += snprintf(url + offset, 1024 - offset, "&left=%ld", len);
  }
  offset += snprintf(url + offset, 1024 - offset, "&compact=1");

  if (data[0] != '\0') memset(data, 0, 1024 * 1024);
  String raw_resp = {.len = 0, .data = data};
  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &raw_resp);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)10);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, (long)10);
  curl_easy_setopt(curl, CURLOPT_SERVER_RESPONSE_TIMEOUT, (long)5);
  curl_easy_setopt(curl, CURLOPT_ACCEPTTIMEOUT_MS, (long)10000);

  TorrentTrackerResponse resp = {0};
  i32 result = curl_easy_perform(curl);
  if (result != CURLE_OK) {
    fprintf(stderr, "----- Communication with tracker an error occurred: %s\n", curl_easy_strerror(result));
    return resp;
  }
  return trackerResponseDecode(raw_resp);
}

u32 trackerPeerListFetch(TorrentMetainfo *metainfo, TorrentTrackerResponse *out, u8 peer_id[20]) {
  (void)peer_id;
  (void)out;
  u32 result = 0;
  // CURL *curl = curl_easy_init();
  // if (!curl) {
  //   curl_easy_cleanup(curl);
  //   curl_global_cleanup();
  //   return 1;
  // }

  u32 fd_count = 0;
  struct pollfd pfds[FD_SIZE] = {0};
  TrackerPollContext trackerpfds[FD_SIZE] = {0};

  for (u32 j = 0; j < metainfo->trackers_count; j++) {
    String tracker_url = metainfo->trackers_url[j];
    printf("\n===| tracker (%d) url: %.*s\n", j, (u32)tracker_url.len, tracker_url.data);
    TorrentTrackerResponse resp = {0};
    (void)resp;
    bool is_udp = tracker_url.data[0] == 'u' && tracker_url.data[1] == 'd' && tracker_url.data[2] == 'p';
    if (is_udp) {
      // resp = trackerUdpFetch(tracker_url, metainfo, udp_tracker_pfds, peer_id);
      if (trackerConnectionStart(j, tracker_url, pfds, &fd_count, trackerpfds) < 0) continue;
    } else {
      continue;
      // resp = trackerHttpFetch(curl, tracker_url, metainfo);
    }
    if (resp.peers.len == 0 && resp.peers6.len == 0) continue;
    *out = resp;
    break;
  }

  i32 poll_count;
  while ((poll_count = poll(pfds, fd_count, 10000)) >= 0) {
    if (poll_count == -1) {
      perror("poll");
      exit(1);
    }

    // Run through connections looking for data to read
    for (i32 i = fd_count - 1; i >= 0; i--) {
      if (pfds[i].fd == 0) continue;
      bool is_ready_to_read = pfds[i].revents & (POLLIN | POLLHUP);
      if (!is_ready_to_read) continue;
      switch (trackerpfds[i].action) {
      case ACTION_CONNECT:
        switch (trackerpfds[i].status) {
        case STATUS_NONE:
          assert(false);
        case STATUS_SENT:
          printf("===| tracker (%d): finished CONNECT\n", trackerpfds[i].idx);
          if (trackerConnectionFinish(pfds, i, trackerpfds) < 0) {
            trackerpfds[i].status = STATUS_FAILED;
          } else {
            trackerpfds[i].status = STATUS_SUCCEED;
          }
          // fallthrough
        case STATUS_SUCCEED:
          printf("===| tracker (%d): starting ANNOUNCE\n", trackerpfds[i].idx);
          if (trackerAnnounceStart(metainfo->info_hash, i, pfds, trackerpfds, peer_id) < 0) {
            trackerpfds[i].status = STATUS_FAILED;
          } else {
            trackerpfds[i].status = STATUS_SENT;
          }
          continue;
        case STATUS_FAILED:
          pfdsDeleteFrom(pfds, trackerpfds, i, &fd_count);
          continue;
        }

      case ACTION_ANNOUNCE:
        switch (trackerpfds[i].status) {
        case STATUS_NONE:
          assert(false);
        case STATUS_SENT:
          if (trackerAnnounceFinish(i, pfds, trackerpfds) < 0) {
            trackerpfds[i].status = STATUS_FAILED;
          } else {
            trackerpfds[i].status = STATUS_SUCCEED;
          }
          // fallthrough
        case STATUS_SUCCEED:
          printf("===| tracker (%d): finished ANNOUNCE\n", trackerpfds[i].idx);
          continue;
        case STATUS_FAILED:
          pfdsDeleteFrom(pfds, trackerpfds, i, &fd_count);
          continue;
        }

      case ACTION_NONE:
        fprintf(stderr, "\ttracker(%d): unitialized trackerpdf\n", i);
        continue;
      }
    }
  }

  // curl_easy_cleanup(curl);
  // curl_global_cleanup();
  return result;
}

void trackerHandshakeGenerate(u8 *info_hash, u8 *peer_id, char handshake_buff[68]) {
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
      printf("Warning: Only wrote %zu of 68 bytes.\n", written);
    }
  } else {
    perror("fopen");
  }
  // Close the file
  if (file) fclose(file);
}

// u32 trackerPeerConnect(i32 fd, struct sockaddr *sock, usize sock_size, char *data, usize data_size) {
//   u32 c = connect(fd, (struct sockaddr *)&sock, sock_size);
//   if (c != 0) {
//     printf("connection with peer failed: %s\n", strerror(errno));
//     return c;
//   }
//
//   if (write(fd, data, data_size) < 0) {
//     printf("failed to send data to peer: %s\n", strerror(errno));
//     return -1;
//   }
//
//   char resp_buff[1024] = {0};
//   if (read(fd, resp_buff, 1023) < 0) {
//     printf("failed to read peer response: %s\n", strerror(errno));
//     return -1;
//   }
//   printf("peer response: %s\n", resp_buff);
//
//   close(fd);
// }

u32 trackerPeer4Handshake(TorrentTrackerResponse *resp, u8 *info_hash, u8 *peer_id) {
  char handshake_buff[68] = {0};
  trackerHandshakeGenerate(info_hash, peer_id, handshake_buff);

  TorrentPeer peer = torrentPeerGet(resp->peers.data, 0);
  char ip_str[INET_ADDRSTRLEN] = {0};
  if (!inet_ntop(AF_INET, peer.ip.data, ip_str, sizeof(ip_str))) {
    printf("  (%d)\t failed to parse ipv: %s\n", 0, strerror(errno));
    return -1;
  }

  u32 result = 0;
  i32 fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    printf("socket error: %s\n", strerror(errno));
    return fd;
  }

  struct sockaddr_in sock = {
      .sin_port = htons(peer.port),
      .sin_family = AF_INET,
  };
  memcpy(&sock.sin_addr, peer.ip.data, peer.ip.len);

  u32 c = connect(fd, (struct sockaddr *)&sock, sizeof(sock));
  if (c != 0) {
    printf("connection with peer failed: %s\n", strerror(errno));
    result = c;
    goto cleanup;
  }

  if (write(fd, handshake_buff, sizeof(handshake_buff)) < 0) {
    printf("failed to send handshake to peer: %s\n", strerror(errno));
    result = -1;
    goto cleanup;
  }

  char resp_buff[1024] = {0};
  if (read(fd, resp_buff, 1023) < 0) {
    printf("failed to read peer response: %s\n", strerror(errno));
    result = -1;
    goto cleanup;
  }
  printf("peer response: %s\n", resp_buff);

cleanup:
  close(fd);
  return result;
}

u32 trackerPeer6Handshake(TorrentTrackerResponse *resp, u8 *info_hash, u8 *peer_id) {
  char handshake_buff[68] = {0};
  trackerHandshakeGenerate(info_hash, peer_id, handshake_buff);

  TorrentPeer6 peer = torrentPeer6Get(resp->peers6.data, 0);
  char ip_str[INET6_ADDRSTRLEN] = {0};
  if (!inet_ntop(AF_INET6, peer.ip.data, ip_str, sizeof(ip_str))) {
    printf("  (%d)\t failed to parse ipv6: %s\n", 0, strerror(errno));
    return -1;
  }

  u32 result = 0;
  i32 fd = socket(AF_INET6, SOCK_STREAM, 0);
  if (fd < 0) {
    printf("socket error: %s\n", strerror(errno));
    return fd;
  }

  struct sockaddr_in sock = {
      .sin_port = htons(peer.port),
      .sin_family = AF_INET,
  };
  memcpy(&sock.sin_addr, peer.ip.data, peer.ip.len);

  u32 c = connect(fd, (struct sockaddr *)&sock, sizeof(sock));
  if (c != 0) {
    printf("connection with peer failed: %s\n", strerror(errno));
    return c;
  }

  if (send(fd, handshake_buff, sizeof(handshake_buff), 0) < 0) {
    printf("failed to send handshake to peer: %s\n", strerror(errno));
    return -1;
  }

  char resp_buff[1024] = {0};
  if (recv(fd, resp_buff, 1023, 0) < 0) {
    printf("failed to read peer response: %s\n", strerror(errno));
    return -1;
  }
  printf("peer response: %s\n", resp_buff);

  close(fd);
  return result;
}
