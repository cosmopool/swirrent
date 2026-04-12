#include <arpa/inet.h>
#include <assert.h>
#include <curl/curl.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include <sys/types.h>
#include <sys/unistd.h>
#include <unistd.h>
#ifdef __linux__
#include <endian.h>
#else
#include <sys/endian.h>
#endif

#include "asio.h"
#include "core.h"
#include "log.h"
#include "threads.h"
#include "torrent.h"
#include "tracker.h"

#define FD_SIZE 400
#define PORT "6666"

static TrackerState trackers[MAX_FD] = {0};
static char data[1024 * 1024] = {0};
static SwirrentOptions *options = {0};

// simple write callback
usize write_cb(char *ptr, usize size, usize nmemb, void *userdata) {
  if (options->verbose) logInfo("----- RESPONSE SIZE: %ld", size * nmemb);
  if (options->verbose) logInfo("----- RESPONSE DATA: %s", ptr);
  if (options->dump_response) {
    FILE *file = fopen(options->raw_request_output_path, "wb");
    if (file) {
      // Write some text to the file
      size_t written = fwrite(ptr, 1, size * nmemb, file);
      if (written < size * nmemb) {
        logInfo("Warning: Only wrote %zu of %zu bytes.", written, size * nmemb);
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
    logInfo("----- Skipping tracker with warning_message: %.*s. Trying another one.",
            (u32)t_resp.warning_message.len, t_resp.warning_message.data);
    return t_resp;
  }
  if (t_resp.failure_reason.len > 0 && t_resp.peers.len == 0) {
    logInfo("----- Skipping tracker because failed: %.*s. Trying another one.",
            (u32)t_resp.failure_reason.len, t_resp.failure_reason.data);
    return t_resp;
  }
  logInfo("\n===| TRACKER RESPONSE");
  logInfo("interval: %ld", t_resp.interval);
  logInfo("min interval: %ld", t_resp.min_interval);
  logInfo("complete: %ld", t_resp.complete);
  logInfo("incomplete: %ld", t_resp.incomplete);
  logInfo("downloaded: %ld", t_resp.downloaded);
  logInfo("warning_message: %.*s", (u32)t_resp.warning_message.len, t_resp.warning_message.data);
  logInfo("failure_reason: %.*s", (u32)t_resp.failure_reason.len, t_resp.failure_reason.data);
  for (u32 i = 0; i < t_resp.peers6.count; i++) {
    if (i == 0) logInfo("peers6:");
    TorrentPeer6 peer = torrentPeer6Get(t_resp.peers6.data, i);
    char ip_buff[INET6_ADDRSTRLEN] = {0};
    if (!inet_ntop(AF_INET6, peer.ip.data, ip_buff, sizeof(ip_buff))) {
      logInfo("  (%d)\t failed to parse ipv6: %s", i, strerror(errno));
      continue;
    }
    logInfo("  (%d)\t ip: %s \t | port: %d", i, ip_buff, peer.port);
  }

  for (u32 i = 0; i < t_resp.peers.len / (IPV4_LEN + PORT_LEN); i++) {
    if (i == 0) logInfo("peers:");
    TorrentPeer peer = torrentPeerGet(t_resp.peers.data, i);
    char ip_buff[INET_ADDRSTRLEN] = {0};
    if (!inet_ntop(AF_INET, peer.ip.data, ip_buff, sizeof(ip_buff))) {
      logInfo("  (%d)\t failed to parse ipv4: %s", i, strerror(errno));
      continue;
    }
    logInfo("  (%d)\t ip: %s \t | port: %d", i, ip_buff, peer.port);
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

i32 trackerAnnounceStart(u8 info_hash[20], u32 fd, u8 peer_id[20]) {
  TorrentTracker tracker = {
      .connection_id = trackers[fd].connection_id,
      .event = TRACKER_EVENT_NONE,
      .port = trackers[fd].port,
  };

  trackers[fd].action = ACTION_ANNOUNCE;
  TrackerAnnounceRequest request = {
      .connection_id = htobe64(trackers[fd].connection_id),
      .action = htobe32(trackers[fd].action),
      .transaction_id = htobe32((u32)rand()),
      .downloaded = htobe64(tracker.downloaded),
      .left = htobe64(tracker.left),
      .uploaded = htobe64(tracker.uploaded),
      .event = htobe32(TRACKER_EVENT_NONE),
      .ip = htobe32(0),
      .key = htobe32(0),
      .num_want = htobe32(-1),
      .port = htobe16(trackers[fd].port),
  };
  memcpy(request.info_hash, info_hash, 20);
  memcpy(request.peer_id, peer_id, 20);
  trackers[fd].transaction_id = be32toh(request.transaction_id);

  if (sendto(fd, &request, ANNOUNCE_SIZE, 0, trackers[fd].addr->ai_addr, trackers[fd].addr->ai_addrlen) < 0) {
    logError("\tfailed to send announce to tracker: %s\n", strerror(errno));
    return -1;
  }

  return 0;
}

i32 trackerAnnounceFinish(u32 fd) {
  u8 buff[2048] = {0};
  TrackerAnnounceResponse *response = (TrackerAnnounceResponse *)buff;
  // TrackerAnnounceResponse *response = malloc(2048);
  // bzero(response, 2048);

  struct sockaddr from;
  socklen_t from_len;
  isize bytes_read = recvfrom(fd, response, sizeof(buff), MSG_WAITALL, &from, &from_len);
  if (bytes_read == 0) {
    logError("\ttracker(%d) announce response: tracker has closed the connection: %s\n", fd, strerror(errno));
    return -1;
  }
  if (bytes_read < 0) {
    // these errors are timeouts
    assert(errno != EAGAIN && errno != EWOULDBLOCK);
    logError("\ttracker(%d) announce response: failed to read tracker response: %s\n", fd, strerror(errno));
    return -1;
  }
  const isize valid_rc = 20;
  if (bytes_read < valid_rc) {
    logError("\ttracker(%d) announce response: invalid tracker connect response: wrong length (%ld)\n", fd, bytes_read);
    return -1;
  }

  if (be32toh(response->action) != ACTION_ANNOUNCE) {
    logError("\ttracker(%d) announce response: invalid tracker announce response: wrong action\n", fd);
    return -1;
  }
  if (be32toh(response->transaction_id) != trackers[fd].transaction_id) {
    logError("\ttracker(%d) announce response: invalid tracker announce response: wrong transaction id\n", fd);
    return -1;
  }
  logInfo("\ttracker(%d): interval: %u", fd, be32toh(response->interval));
  logInfo("\ttracker(%d): leechers: %u", fd, be32toh(response->leechers));
  logInfo("\ttracker(%d): seeders: %u", fd, be32toh(response->seeders));
  logInfo("\ttracker(%d): peers %lu:", fd, (bytes_read - sizeof(*response)) / 6);
  logInfo("\ttracker(%d): response size %lu:", fd, bytes_read);
  for (u32 j = 0; j < (bytes_read - sizeof(*response)) / 6; j++) {
    TorrentPeer peer = torrentPeerGet((char *)response->peers, j);
    char buf[INET_ADDRSTRLEN] = {0};
    if (!inet_ntop(AF_INET, peer.ip.data, buf, sizeof(buf))) {
      logError("\t(%d)\t failed to parse ipv4: %s\n", j, strerror(errno));
      return -1;
    }
    logInfo("\t(%d)\t ip: %s\t | port: %d", j, buf, peer.port);
  }
  logInfo("");

  return 0;
  // cleanup:
  //   if (response != NULL) free(response);
  //   return -1;
}

i32 asdf(void *url, void *out) {
  struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_DGRAM};
  // resolve tracker ip
  char host[256], port[16];
  parse_tracker_url(*(String *)url, host, sizeof(host), port, sizeof(port));
  // TODO: turn getaddrinfo call into asynchronous because it blocks
  i32 get_addr_status;
  if ((get_addr_status = getaddrinfo(host, port, &hints, (struct addrinfo **)&out)) != 0) {
    logError("\tgetaddrinfo: %s\n", gai_strerror(get_addr_status));
    return -1;
  }
  return 0;
}

i32 trackerConnectionStart(TrackerState *tracker) {
  ASSERT(tracker->addr, "the tracker address info must have bing set already");

  // print ipv4 of tracker
  struct sockaddr_in *ipv4 = (struct sockaddr_in *)(void *)tracker->addr->ai_addr;
  char ip[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &ipv4->sin_addr, ip, sizeof(ip));
  logInfo("\topening socket for: %s:%d", ip, ntohs(ipv4->sin_port));
  // nintendo switch only supports ipv4
  assert(tracker->addr->ai_family == AF_INET);
  assert(tracker->addr->ai_socktype == SOCK_DGRAM);

  // opening socket
  i32 fd = socket(tracker->addr->ai_family, tracker->addr->ai_socktype, tracker->addr->ai_protocol);
  if (fd < 0) {
    logError("\tsocket error: %s\n", strerror(errno));
    return fd;
  }

  // bind to any port
  struct sockaddr_in src = {
      .sin_family = AF_INET,
      .sin_addr.s_addr = htonl(INADDR_ANY),
      .sin_port = htons(0),
  };
  if (bind(fd, (struct sockaddr *)&src, sizeof(src)) < 0) {
    logError("\tfailed to bind to tracker fd: %s\n", strerror(errno));
    close(fd);
    return -1;
  }

  // send connect request
  u64 protocol_fixed_value = 0x41727101980;
  u32 action = ACTION_CONNECT;
  TrackerConnectRequest req = {
      .protocol_id = htobe64(protocol_fixed_value),
      .action = htobe32(action),
      .transaction_id = htobe32((u32)rand()),
  };
  assert(sizeof(req) == 16);

  logInfo("\t--- sending connect request to tracker");
  if (sendto(fd, &req, CONNECT_REQUEST_SIZE, 0, tracker->addr->ai_addr, tracker->addr->ai_addrlen) < 0) {
    logError("\tfailed to send connect request to tracker: %s\n", strerror(errno));
    close(fd);
    return -1;
  }

  tracker->action = ACTION_CONNECT;
  tracker->status = STATUS_SENT;
  tracker->transaction_id = be32toh(req.transaction_id);
  tracker->action = be32toh(req.action);
  tracker->port = be16toh(ipv4->sin_port);

  return fd;
}

i32 trackerConnectionFinish(i32 fd) {
  TrackerConnectResponse *response = malloc(1024);
  struct sockaddr from;
  socklen_t from_len;
  isize bytes_read = recvfrom(fd, response, sizeof(*response), MSG_WAITALL, &from, &from_len);
  if (bytes_read == 0) {
    logError("\ttracker (%d) connect response: connection was closed by tracker: %s\n", fd, strerror(errno));
    return -1;
  }
  if (bytes_read < 0) {
    // these errors are timeouts
    assert(errno != EAGAIN || errno != EWOULDBLOCK);
    logError("\ttracker (%d) connect response: failed to read response: %s\n", fd, strerror(errno));
    return -1;
  }
  const isize valid_length = 16;
  if (bytes_read < valid_length) {
    logError("\ttracker (%d) connect response: wrong length (%ld)\n", fd, bytes_read);
    return -1;
  }
  // check tracker connect response
  if (be32toh(response->transaction_id) != trackers[fd].transaction_id) {
    logError("\ttracker (%d) connect response: invalid transaction_id in response!\n", fd);
    return -1;
  }
  if (be32toh(response->action) != trackers[fd].action) {
    logError("\ttracker (%d) connect response: action in response!\n", fd);
    return -1;
  }
  trackers[fd].connection_id = be64toh(response->connection_id);
  logInfo("\ttracker (%d): successfully connected with id: %llu", trackers[fd].id, trackers[fd].connection_id);
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
    logError("----- Communication with tracker an error occurred: %s\n", curl_easy_strerror(result));
    return resp;
  }
  return trackerResponseDecode(raw_resp);
}

void freeTrackerState(TrackerState *t) {
  if (t->addr) freeaddrinfo(t->addr);
  *t = (TrackerState){.action = ACTION_NONE};
}

void trackerStateResolver(i32 fd, void *m, u8 peer_id[20]) {
  TorrentMetainfo *metainfo = m;

  logInfo("===| tracker (%d)", trackers[fd].id);
  switch (trackers[fd].action) {
  case ACTION_CONNECT:
    switch (trackers[fd].status) {
    case STATUS_NONE:
      UNREACHABLE("there should be no unitialized tracker at this point");
    case STATUS_SENT:
      logInfo("\tCONNECT decoding response");
      if (trackerConnectionFinish(fd) < 0) {
        trackers[fd].status = STATUS_FAILED;
      } else {
        trackers[fd].status = STATUS_SUCCEED;
      }
      trackerStateResolver(fd, m, peer_id);
      return;
    case STATUS_SUCCEED:
      logInfo("\tCONNECT succeed");
      logInfo("\tANNOUNCE sent");
      if (trackerAnnounceStart(metainfo->info_hash, fd, peer_id) < 0) {
        trackers[fd].status = STATUS_FAILED;
        trackerStateResolver(fd, m, peer_id);
      } else {
        trackers[fd].status = STATUS_SENT;
      }
      return;
    case STATUS_FAILED:
      logInfo("\tCONNECT failed");
      asioFdUnset(fd);
      freeTrackerState(trackers + fd);
      return;
    }

  case ACTION_ANNOUNCE:
    switch (trackers[fd].status) {
    case STATUS_NONE:
      UNREACHABLE("there should be no unitialized tracker at this point");
    case STATUS_SENT:
      logInfo("\tANNOUNCE decoding response");
      if (trackerAnnounceFinish(fd) < 0) {
        trackers[fd].status = STATUS_FAILED;
      } else {
        trackers[fd].status = STATUS_SUCCEED;
      }
      trackerStateResolver(fd, m, peer_id);
      return;
    case STATUS_SUCCEED:
      logInfo("\tANNOUNCE succeed");
      asioFdUnset(fd);
      freeTrackerState(trackers + fd);
      return;
    case STATUS_FAILED:
      logInfo("\tANNOUNCE failed");
      asioFdUnset(fd);
      freeTrackerState(trackers + fd);
      return;
    }

  case ACTION_NONE:
    UNREACHABLE("there should be no unitialized tracker at this point");
    break;
  }
}

u32 trackerPeerListFetch(TorrentMetainfo *metainfo, TorrentTrackerResponse *out, u8 peer_id[20]) {
  (void)out;
  u32 result = 0;
  // CURL *curl = curl_easy_init();
  // if (!curl) {
  //   curl_easy_cleanup(curl);
  //   curl_global_cleanup();
  //   return 1;
  // }

  for (u32 j = 0; j < metainfo->trackers_count; j++) {
    String url = metainfo->trackers_url[j];
    logInfo("\n===| tracker (%d) url: %.*s", j, (u32)url.len, url.data);
    bool is_udp = url.data[0] == 'u' && url.data[1] == 'd' && url.data[2] == 'p';
    if (!is_udp) continue;

    ThreadJob job = {.id = j, .args = (void *)&url, .callback = asdf};
    threadJobCreate(job);
  }
  return 0;

  isize remaing = metainfo->trackers_count;
  while (remaing > 0) {
    ThreadJob job = threadGetCompletedJob();
    if (threadJobIsEmpty(job)) {
      svcSleepThread(30 * NANOSECONDS_IN_MILLI);
      continue;
    }
    logInfo("\tCONNECT sent");

    i32 fd = trackerConnectionStart(trackers + job.id);
    if (fd < 0) {
      logInfo("\tCONNECT failed");
      freeTrackerState(trackers + job.id);
      remaing--;
      continue;
    }
    asioFdSet((AsioFd){.fd = fd, .on_ready_callback = trackerStateResolver});

    remaing--;
  }

  asioWaitForEvents(metainfo, peer_id);
  asioUnsetAll();
  for (u32 i = 0; i <= MAX_FD; i++) {
    if (trackers[i].id == 0) continue;
    freeTrackerState(trackers + i);
  }

  // TorrentTrackerResponse resp = {0};
  // if (resp.peers.len == 0 && resp.peers6.len == 0) continue;
  // *out = resp;
  // break;
  // return 0;

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
      logInfo("Warning: Only wrote %zu of 68 bytes.", written);
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
//     logInfo("connection with peer failed: %s", strerror(errno));
//     return c;
//   }
//
//   if (write(fd, data, data_size) < 0) {
//     logInfo("failed to send data to peer: %s", strerror(errno));
//     return -1;
//   }
//
//   char resp_buff[1024] = {0};
//   if (read(fd, resp_buff, 1023) < 0) {
//     logInfo("failed to read peer response: %s", strerror(errno));
//     return -1;
//   }
//   logInfo("peer response: %s", resp_buff);
//
//   close(fd);
// }

u32 trackerPeer4Handshake(TorrentTrackerResponse *resp, u8 *info_hash, u8 *peer_id) {
  char handshake_buff[68] = {0};
  trackerHandshakeGenerate(info_hash, peer_id, handshake_buff);

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

u32 trackerPeer6Handshake(TorrentTrackerResponse *resp, u8 *info_hash, u8 *peer_id) {
  char handshake_buff[68] = {0};
  trackerHandshakeGenerate(info_hash, peer_id, handshake_buff);

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
