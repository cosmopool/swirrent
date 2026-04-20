#include <arpa/inet.h>
#include <assert.h>
#include <curl/curl.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/unistd.h>
#include <time.h>
#include <unistd.h>
#ifdef __linux__
#include <endian.h>
#else
#include <sys/endian.h>
#endif

#include "asio.h"
#include "core.h"
#include "log.h"
#include "peer.h"
#include "threads.h"
#include "torrent.h"
#include "tracker.h"

#define FD_SIZE 400
#define PORT "6666"
#define MAX_TRIES 2

static TrackerState trackers[MAX_FD] = {0};
static i32 fd_to_tracker_idx[MAX_FD] = {0};
static char data[1024 * 1024] = {0};
static SwirrentOptions *options = {0};
static TorrentPeers peers = {0};

TrackerState *trackerStateFromFd(i32 fd) {
  i32 tracker_idx = fd_to_tracker_idx[fd];
  ASSERT(tracker_idx > 0, "tracker indexes must be positive");
  TrackerState *tracker = trackers + tracker_idx;
  ASSERT((i32)tracker->id == tracker_idx, "must be the same id");
  return tracker;
}

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

TorrentTrackerResponse trackerHttpResponseDecode(String resp) {
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
    size_t port_size = strlen(port_start);
    if (port_end) {
      port_size = (size_t)(port_end - port_start);
    }
    if (port_size >= port_len) port_size = port_len - 1;
    strncpy(port, port_start, port_size);
    port[port_size] = '\0';
  } else {
    strcpy(port, "80");
  }
}

i32 trackerAnnounceStart(u8 info_hash[20], u32 fd, u8 peer_id[20], u64 now) {
  TrackerState *ts = trackerStateFromFd(fd);
  ts->tries++;
  ts->last_try = now;

  TorrentTracker tracker = {
      .connection_id = ts->connection_id,
      .event = TRACKER_EVENT_NONE,
      .port = ts->port,
  };

  ts->action = ACTION_ANNOUNCE;
  TrackerAnnounceRequest request = {
      .connection_id = htobe64(ts->connection_id),
      .action = htobe32(ts->action),
      .transaction_id = htobe32((u32)rand()),
      .downloaded = htobe64(tracker.downloaded),
      .left = htobe64(tracker.left),
      .uploaded = htobe64(tracker.uploaded),
      .event = htobe32(TRACKER_EVENT_NONE),
      .ip = htobe32(0),
      .key = htobe32(0),
      .num_want = htobe32(-1),
      .port = htobe16(ts->port),
  };
  memcpy(request.info_hash, info_hash, 20);
  memcpy(request.peer_id, peer_id, 20);
  ts->transaction_id = be32toh(request.transaction_id);

  if (sendto(fd, &request, ANNOUNCE_SIZE, 0, ts->addr->ai_addr, ts->addr->ai_addrlen) < 0) {
    logError("\tfailed to send announce to tracker: %s\n", strerror(errno));
    return -1;
  }

  logInfo("\tANNOUNCE sent");
  return 0;
}

i32 trackerAnnounceFinish(u32 fd) {
  logInfo("\tANNOUNCE decoding response");
  u8 buff[2048] = {0};
  TrackerAnnounceResponse *response = (TrackerAnnounceResponse *)buff;
  struct sockaddr from;
  socklen_t from_len;
  isize bytes_read = recvfrom(fd, response, sizeof(buff), MSG_WAITALL, &from, &from_len);
  if (bytes_read == 0) {
    logError("\tannounce response: tracker has closed the connection: %s\n", strerror(errno));
    return -1;
  }
  if (bytes_read < 0) {
    logError("\tannounce response: failed to read tracker response: %s\n", strerror(errno));
    // these errors are timeouts
    assert(errno != EAGAIN && errno != EWOULDBLOCK);
    return -1;
  }
  const isize valid_rc = 20;
  if (bytes_read < valid_rc) {
    logError("\tannounce response: invalid tracker connect response: wrong length (%ld)\n", bytes_read);
    return -1;
  }

  if (be32toh(response->action) != ACTION_ANNOUNCE) {
    logError("\tannounce response: invalid tracker announce response: wrong action\n");
    return -1;
  }
  TrackerState *tracker = trackerStateFromFd(fd);
  if (be32toh(response->transaction_id) != tracker->transaction_id) {
    logError("\announce response: invalid tracker announce response: wrong transaction id\n");
    return -1;
  }
  u32 old_len = peers.len;
  u32 num_peers = (bytes_read - sizeof(*response)) / (IPV4_LEN + PORT_LEN);
  torrentAddPeers(&peers, response->peers, num_peers);
  logInfo("\ttracker: interval: %u", be32toh(response->interval));
  logInfo("\ttracker: leechers: %u", be32toh(response->leechers));
  logInfo("\ttracker: seeders: %u", be32toh(response->seeders));
  logInfo("\ttracker: peers count: %lu", num_peers);
  logInfo("\ttracker: peers len: %lu", bytes_read - sizeof(*response));
  logInfo("\ttracker: response size: %lu", bytes_read);

  ASSERT(memcmp(peers.data + old_len, response->peers, num_peers * (IPV4_LEN + PORT_LEN)) == 0, "same value");
  logInfo("");
  logInfo("===== all available peers:");
  for (u32 i = 0; i < peers.count; i++) {
    TorrentPeer peer = torrentPeerGet(peers.data, i);
    char buf[INET_ADDRSTRLEN] = {0};
    if (!inet_ntop(AF_INET, peer.ip.data, buf, sizeof(buf))) {
      logError("\t failed to parse ipv4: %s\n", i, strerror(errno));
      return -1;
    }
    logInfo("\t ip: %s\t | port: %d", buf, peer.port);
  }
  logInfo("");

  return 0;
}

i32 trackerResolveAddress(void *url, void **out) {
  struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_DGRAM};
  char host[256], port[16];
  parse_tracker_url(*(String *)url, host, sizeof(host), port, sizeof(port));
  i32 get_addr_status;
  if ((get_addr_status = getaddrinfo(host, port, &hints, (struct addrinfo **)out)) != 0) {
    logError("\tgetaddrinfo: %s\n", gai_strerror(get_addr_status));
    return -1;
  }

  String *s = url;
  struct addrinfo *addr = *out;
  struct sockaddr_in *ipv4 = (struct sockaddr_in *)(void *)addr->ai_addr;
  char ip[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &ipv4->sin_addr, ip, sizeof(ip));
  logInfo("tracker url: %.*s | ip: %s", (u32)s->len, s->data, ip);
  return 0;
}

i32 trackerSockOpen(TrackerState *tracker) {
  ASSERT(tracker->addr, "the tracker address info must have being set already");

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

  return fd;
}

i32 trackerConnectionStart(i32 fd, u64 now) {
  TrackerState *tracker = trackerStateFromFd(fd);
  tracker->tries++;
  tracker->last_try = now;

  // send connect request
  u64 protocol_fixed_value = 0x41727101980;
  u32 action = ACTION_CONNECT;
  TrackerConnectRequest req = {
      .protocol_id = htobe64(protocol_fixed_value),
      .action = htobe32(action),
      .transaction_id = htobe32((u32)rand()),
  };
  ASSERT(sizeof(req) == 16, "the connection request has a fixed size");

  if (sendto(fd, &req, CONNECT_REQUEST_SIZE, 0, tracker->addr->ai_addr, tracker->addr->ai_addrlen) < 0) {
    logError("\tfailed to send connect request to tracker: %s\n", strerror(errno));
    close(fd);
    return -1;
  }

  struct sockaddr_in *ipv4 = (struct sockaddr_in *)(void *)tracker->addr->ai_addr;
  tracker->action = ACTION_CONNECT;
  tracker->transaction_id = be32toh(req.transaction_id);
  tracker->action = be32toh(req.action);
  tracker->port = be16toh(ipv4->sin_port);

  logInfo("\tCONNECT sent");
  return 0;
}

i32 trackerConnectionFinish(i32 fd) {
  logInfo("\tCONNECT decoding response");
  TrackerConnectResponse *response = malloc(1024);
  struct sockaddr_storage from;
  socklen_t from_len = sizeof(from);
  isize bytes_read = recvfrom(fd, response, sizeof(*response), MSG_WAITALL, (struct sockaddr *)&from, &from_len);
  if (bytes_read == 0) {
    logError("connection was closed by tracker: %s\n", strerror(errno));
    return -1;
  }
  if (bytes_read < 0) {
    logError("failed to read response: %s\n", strerror(errno));
    // these errors are timeouts
    assert(errno != EAGAIN || errno != EWOULDBLOCK);
    return -1;
  }
  const isize valid_length = 16;
  if (bytes_read < valid_length) {
    logError("wrong length (%ld)\n", bytes_read);
    return -1;
  }
  TrackerState *tracker = trackerStateFromFd(fd);
  // check tracker connect response
  if (be32toh(response->transaction_id) != tracker->transaction_id) {
    logError("invalid transaction_id in response!\n");
    return -1;
  }
  if (be32toh(response->action) != tracker->action) {
    logError("action in response!\n");
    return -1;
  }
  tracker->connection_id = be64toh(response->connection_id);
  logInfo("\tsuccessfully connected with id: %llu", tracker->connection_id);
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
  return trackerHttpResponseDecode(raw_resp);
}

void freeTrackerState(TrackerState *t) {
  if (t->addr != NULL) freeaddrinfo(t->addr);
  *t = (TrackerState){.action = ACTION_NONE};
}

u16 trackerTimeoutCalculate(TrackerState *tracker) {
  if (tracker->tries > 8) return 3840;
  u8 tries = tracker->tries > 8 ? 8 : tracker->tries;
  u32 timeout = 15u << tries;
  ASSERT(timeout <= 3840, "timeout can only increase up to 3840 seconds");
  return timeout;
}

bool trackerHasTimeoutExpired(i32 fd, u64 now) {
  ASSERT(now > 0, "now timestamp should not be 0");
  TrackerState *tracker = trackerStateFromFd(fd);
  if (tracker->last_try <= 0) return false;
  u64 diff = now - tracker->last_try;
  return diff > trackerTimeoutCalculate(tracker);
}

void trackerStateResolver(i32 fd, u64 now, ASIO_STATUS asio_status, void *asio_args) {
  AsioArgs *args = asio_args;
  TrackerState *tracker = trackerStateFromFd(fd);
  logInfo("===| tracker (%d) | fd (%d)", tracker->id, fd);
  logInfo("\ttries: %d", tracker->tries);
  logInfo("\ttimeout: %ds", trackerTimeoutCalculate(tracker));
  if (tracker->tries > MAX_TRIES) {
    logInfo("\texceed max tries (%d), will unset fd and tracker", tracker->tries);
    asioFdUnset(fd);
    freeTrackerState(tracker);
    return;
  }
  ASSERT(tracker->tries <= MAX_TRIES, "tries can't exceed MAX_TRIES");

  switch (tracker->action) {
  case ACTION_NONE:
    if (trackerConnectionStart(fd, now) < 0) break;
    return;

  case ACTION_CONNECT:
    if (asio_status == ASIO_TIMEOUT) {
      if (trackerConnectionStart(fd, now) < 0) break;
      return;
    }

    tracker->tries = 0;
    if (trackerConnectionFinish(fd) < 0) break;

    if (trackerAnnounceStart(args->info_hash, fd, args->peer_id, now) < 0) break;
    return;

  case ACTION_ANNOUNCE:
    if (asio_status == ASIO_TIMEOUT) {
      if (trackerAnnounceStart(args->info_hash, fd, args->peer_id, now) < 0) break;
      return;
    }

    tracker->tries = 0;
    if (trackerAnnounceFinish(fd) < 0) break;
    break;
  }

  asioFdUnset(fd);
  freeTrackerState(tracker);
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

  AsioArgs asio_args = {.info_hash = metainfo->info_hash, .peer_id = peer_id};
  peers.data = calloc(5, IPV4_LEN + PORT_LEN);
  isize remaining = 0;
  for (u32 j = 0; j < metainfo->trackers_count; j++) {
    String url = metainfo->trackers_url[j];
    logInfo("\n===| tracker (%d) url: %.*s", j, (u32)url.len, url.data);
    bool is_udp = url.data[0] == 'u' && url.data[1] == 'd' && url.data[2] == 'p';
    if (!is_udp) continue;
    remaining++;
    ThreadJob job = {.tracker_id = j, .args = metainfo->trackers_url + j, .callback = trackerResolveAddress};
    threadsJobEnqueue(job);
  }

  while (remaining > 0) {
    ThreadJob job = {0};
    bool has_pending_jobs = false;
    while ((has_pending_jobs = threadsPoolHasWork())) {
      job = threadsJobTakeFinished();
      if (!threadsJobIsEmpty(job)) break;
      struct timespec remaining_t, request_t = {5, 30 * NANOSECONDS_IN_MILLI};
      nanosleep(&request_t, &remaining_t);
    }
    if (!has_pending_jobs) {
      ASSERT(threadsJobIsEmpty(job), "finalized queue should be empty when pending is 0");
      ASSERT(remaining == 0, "all trackers should have being processed by now");
      break;
    }
    remaining--;
    if (job.error != 0) {
      logInfo("not able to resolve tracker (%d) address. failed with error: %d", job.tracker_id, job.error);
      continue;
    }
    ASSERT(!threadsJobIsEmpty(job), "an empty job is invalid here");
    ASSERT(job.results, "the tracker addrs should be resolved by now");
    trackers[job.tracker_id].id = job.tracker_id;
    trackers[job.tracker_id].addr = job.results;
    trackers[job.tracker_id].action = ACTION_NONE;
    trackers[job.tracker_id].url = metainfo->trackers_url[job.tracker_id];
    logInfo("[TRACKER] opening sock for tracker %d", job.tracker_id);
    i32 fd = trackerSockOpen(trackers + job.tracker_id);
    if (fd < 0) {
      freeTrackerState(trackers + job.tracker_id);
      continue;
    }
    fd_to_tracker_idx[fd] = job.tracker_id;
    asioFdSet((AsioFd){
        .fd = fd,
        .args = &asio_args,
        .on_ready_callback = trackerStateResolver,
        .has_timeout_expired_callback = trackerHasTimeoutExpired,
    });
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) {
      logError("[TRACKER] error fetching current time: %s", strerror(errno));
      continue;
    }
    trackerStateResolver(fd, ts.tv_sec, ASIO_NONE, &asio_args);
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
