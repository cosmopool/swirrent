#include "metainfo.h"
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
#include "threads.h"
#include "tracker.h"

typedef struct {
  TorrentMetainfo *metainfo;
  u8 *peer_id;
  add_peer_callback add_callback;
} AsioArgs;

static TrackerState trackers[MAX_FD] = {0};
static i32 fd_to_tracker_idx[MAX_FD] = {0};

TrackerState *trackerStateFromFd(i32 fd) {
  i32 tracker_idx = fd_to_tracker_idx[fd];
  ASSERT(tracker_idx > 0, "tracker indexes must be positive");
  TrackerState *tracker = trackers + tracker_idx;
  ASSERT((i32)tracker->id == tracker_idx, "must be the same id");
  return tracker;
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

i32 trackerAnnounceStart(TorrentMetainfo *metainfo, u32 fd, u8 peer_id[20], u64 now) {
  TrackerState *ts = trackerStateFromFd(fd);
  ts->tries++;
  ts->last_try = now;

  Tracker tracker = {
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
  memcpy(request.info_hash, metainfo->info_hash, 20);
  memcpy(request.peer_id, peer_id, 20);
  ts->transaction_id = be32toh(request.transaction_id);

  if (sendto(fd, &request, ANNOUNCE_SIZE, 0, ts->addr->ai_addr, ts->addr->ai_addrlen) < 0) {
    logError("\tfailed to send announce to tracker: %s\n", strerror(errno));
    return -1;
  }

  logInfo("\tANNOUNCE sent");
  return 0;
}

i32 trackerAnnounceFinish(u32 fd, AsioArgs args) {
  logInfo("\tANNOUNCE decoding response");
  u8 buff[2048] = {0};
  TrackerAnnounceResponse *response = (TrackerAnnounceResponse *)buff;
  struct sockaddr from;
  socklen_t from_len;
  isize bytes_read = recvfrom(fd, response, sizeof(buff), MSG_WAITALL, &from, &from_len);
  if (bytes_read == 0) {
    logError("\ttracker has closed the connection: %s\n", strerror(errno));
    return -1;
  }
  if (bytes_read < 0) {
    logError("\tfailed to read tracker response: %s\n", strerror(errno));
    // these errors are timeouts
    assert(errno != EAGAIN && errno != EWOULDBLOCK);
    return -1;
  }
  const isize valid_rc = 20;
  if (bytes_read < valid_rc) {
    logError("\tbad response length (%ld)", bytes_read);
    return -1;
  }

  if (be32toh(response->action) != ACTION_ANNOUNCE) {
    logError("\tbad response action: %d", response->action);
    return -1;
  }
  TrackerState *tracker = trackerStateFromFd(fd);
  if (be32toh(response->transaction_id) != tracker->transaction_id) {
    logError("\tbad response transaction id: %d", response->transaction_id);
    return -1;
  }
  u32 peers_count = (bytes_read - sizeof(*response)) / (IPV4_LEN + PORT_LEN);
  logInfo("\tinterval: %u", be32toh(response->interval));
  logInfo("\tleechers: %u", be32toh(response->leechers));
  logInfo("\tseeders: %u", be32toh(response->seeders));
  logInfo("\tpeers count: %lu", peers_count);
  logInfo("\tpeers byte len: %lu", bytes_read - sizeof(*response));
  logInfo("\tresponse size: %lu", bytes_read);
  args.add_callback(response->peers, IPV4_LEN, peers_count, args.metainfo, args.peer_id);
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
  u8 buf[1024] = {0};
  TrackerConnectResponse *response = (TrackerConnectResponse *)buf;
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

    if (trackerAnnounceStart(args->metainfo, fd, args->peer_id, now) < 0) break;
    return;

  case ACTION_ANNOUNCE:
    if (asio_status == ASIO_TIMEOUT) {
      if (trackerAnnounceStart(args->metainfo, fd, args->peer_id, now) < 0) break;
      return;
    }

    tracker->tries = 0;
    if (trackerAnnounceFinish(fd, *args) < 0) break;
    break;
  }

  asioFdUnset(fd);
  freeTrackerState(tracker);
}

u32 trackerPeerListFetch(String *trackers_url, usize trackers_count, TorrentMetainfo *metainfo, u8 peer_id[PEER_ID_LENGTH], add_peer_callback add_callback) {
  u32 result = 0;
  // CURL *curl = curl_easy_init();
  // if (!curl) {
  //   curl_easy_cleanup(curl);
  //   curl_global_cleanup();
  //   return 1;
  // }

  AsioArgs asio_args = {.metainfo = metainfo, .peer_id = peer_id, .add_callback = add_callback};
  isize remaining = 0;
  for (u32 j = 0; j < trackers_count; j++) {
    String url = trackers_url[j];
    logInfo("\n===| tracker (%d) url: %.*s", j, (u32)url.len, url.data);
    bool is_udp = url.data[0] == 'u' && url.data[1] == 'd' && url.data[2] == 'p';
    if (!is_udp) continue;
    remaining++;
    ThreadJob job = {.tracker_id = j, .args = trackers_url + j, .callback = trackerResolveAddress};
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
    trackers[job.tracker_id].url = trackers_url[job.tracker_id];
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

  asioWaitForEvents();
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
