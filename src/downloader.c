#include "bencode.h"
#include "curl/system.h"
#include "torrent.h"

#include <arpa/inet.h>
#include <assert.h>
#include <curl/curl.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/unistd.h>

#define STRING_IMPLEMENTATION
#include "core.h"
#include "downloader.h"

static usize peer_count = 0;
static TorrentPeer peers[2048] = {0};
static char data[1024 * 1024] = {0};
static Options *options = {0};

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

void downloaderOptionsSet(Options *op) {
  options = op;
}

u32 downloaderTrackerResponseDecode(String resp, TorrentMetainfo *metainfo, TorrentTrackerResponse *out) {
  BencodeParser encoder = bencodeParserFromData((char *)resp.data, resp.len);
  TorrentTrackerResponse t_resp = {.peers = peers};
  bencodeTrackerResponseDecode(&encoder, metainfo, &t_resp);

  if (t_resp.warning_message.len > 0 && t_resp.peer_count == 0) {
    printf("----- Skipping tracker with warning_message: %.*s. Trying another one.\n",
           (u32)t_resp.warning_message.len, t_resp.warning_message.data);
    return 1;
  }
  if (t_resp.failure_reason.len > 0 && t_resp.peer_count == 0) {
    printf("----- Skipping tracker because failed: %.*s. Trying another one.\n",
           (u32)t_resp.failure_reason.len, t_resp.failure_reason.data);
    return 1;
  }
  printf("\n===| TRACKER RESPONSE\n");
  printf("interval: %ld\n", t_resp.interval);
  printf("min interval: %ld\n", t_resp.min_interval);
  printf("complete: %ld\n", t_resp.complete);
  printf("incomplete: %ld\n", t_resp.incomplete);
  printf("downloaded: %ld\n", t_resp.downloaded);
  printf("warning_message: %.*s\n", (u32)t_resp.warning_message.len, t_resp.warning_message.data);
  printf("failure_reason: %.*s\n", (u32)t_resp.failure_reason.len, t_resp.failure_reason.data);
  for (u32 i = 0; i < t_resp.peer_count; i++) {
    if (i == 0) printf("peers:\n");
    u32 val = t_resp.peers[i].ip;
    printf("  (%d)\t ip: %d.%d.%d.%d \t | port: %d\n", i,
           (val & 0xFF000000) >> 24, (val & 0x00FF0000) >> 16,
           (val & 0x0000FF00) >> 8, val & 0x000000FF, t_resp.peers[i].port);
  }
  peer_count = t_resp.peer_count;
  *out = t_resp;
  return 0;
}

u32 downloaderTrackerPeerListFetch(TorrentMetainfo *metainfo) {
  u32 result = 0;
  CURL *curl = curl_easy_init();
  if (!curl) {
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return 1;
  }

  for (u32 j = 0; j < metainfo->trackers_count; j++) {
    String tracker_url = metainfo->trackers_url[j];
    printf("\n===| tracker (%d) url: %.*s\n", j, (u32)tracker_url.len, tracker_url.data);
    if (tracker_url.data[0] == 'u' && tracker_url.data[1] == 'd' && tracker_url.data[2] == 'p') {
      printf("----- Only http trackers are supported. Skipping.\n");
      continue;
    }

    // curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

    char url[1024] = {0};
    snprintf(url, tracker_url.len + 1, "%s", tracker_url.data);
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

    usize offset = 0;
    char encoded_hash[61] = {0};
    for (int i = 0; i < SHA_DIGEST_LENGTH; ++i) {
      offset += sprintf(encoded_hash + offset, "%%%02x", (u8)metainfo->info_hash[i]);
    }
    sprintf(url, "%sinfo_hash=%s", url, encoded_hash);
    sprintf(url, "%s&peer_id=%s", url, "k492jal1dkfj9oa3e8se");
    sprintf(url, "%s&port=%s", url, "6881");
    sprintf(url, "%s&uploaded=%s", url, "0");
    sprintf(url, "%s&downloaded=%s", url, "0");
    if (metainfo->info.is_single_file) {
      sprintf(url, "%s&left=%ld", url, metainfo->info.length);
    } else {
      usize len = 0;
      for (u32 i = 0; i < metainfo->info.multi_files.count; i++) {
        len += metainfo->info.multi_files.files[i].length;
      }
      sprintf(url, "%s&left=%ld", url, len);
    }
    sprintf(url, "%s&compact=1", url);

    if (data[0] != '\0') memset(data, 0, 1024 * 1024);
    String resp = {.len = 0, .data = data};
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)5);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, (long)5);
    curl_easy_setopt(curl, CURLOPT_SERVER_RESPONSE_TIMEOUT, (long)5);
    curl_easy_setopt(curl, CURLOPT_ACCEPTTIMEOUT_MS, (long)5000);

    result = curl_easy_perform(curl);
    if (result != CURLE_OK) {
      fprintf(stderr, "----- Communication with tracker an error occurred: %s\n", curl_easy_strerror(result));
      continue;
    }

    TorrentTrackerResponse t_resp = {.peers = peers};
    u32 tracker_result = downloaderTrackerResponseDecode(resp, metainfo, &t_resp);
    if (tracker_result != 0) continue;
    if (t_resp.peer_count == 0) continue;
    break;
  }

  curl_easy_cleanup(curl);
  curl_global_cleanup();
  return result;
}

u32 downloaderPeerHandshake(TorrentTrackerResponse *resp, char *info_hash, u8 *peer_id) {
  assert(info_hash[0] != '\0');

  // struct sockaddr_in sock;
  // char buff[1024] = {0};
  // char *data = {0};
  TorrentPeer peer = resp->peers[0];

  const char *protocol_str = "BitTorrent protocol";
  const u32 protocol_len = strlen(protocol_str);
  const char reserved[8] = {0};
  char handshake_buff[68] = {0};

  u32 offset = 0;
  // const usize bufflen = 1 + 19 + sizeof(reserved) + SHA_DIGEST_LENGTH + PEER_ID_LENGTH;
  memcpy(handshake_buff, protocol_str, protocol_len);
  offset += protocol_len + 1;
  assert(offset == 20);

  // memcpy(handshake_buff + offset, reserved, sizeof(reserved));
  // offset += sizeof(reserved);
  // u8 t = 0;
  // for (u32 i = 0; i < 8; i++) {
  //   memcpy(handshake_buff + offset, &t, 1);
  //   offset++;
  // }
  u8 t = 0;
  handshake_buff[offset + 1] = t;
  handshake_buff[offset + 2] = t;
  handshake_buff[offset + 3] = t;
  handshake_buff[offset + 4] = t;
  handshake_buff[offset + 5] = t;
  handshake_buff[offset + 6] = t;
  handshake_buff[offset + 7] = t;
  handshake_buff[offset + 8] = t;
  offset += sizeof(reserved);
  assert(offset == 28);

  memcpy(handshake_buff + offset, info_hash, SHA_DIGEST_LENGTH);
  offset += SHA_DIGEST_LENGTH;
  assert(offset == 48);

  memcpy(handshake_buff + offset, peer_id, PEER_ID_LENGTH);
  offset += PEER_ID_LENGTH;
  assert(offset == 68);
  // printf("handshake hash: %s\n", info_hash);
  // printf("handshake   id: %s\n", peer_id);
  printf("handshake data: %.*s\n", 68, handshake_buff);
  // FILE *file = fopen("handshake", "wb");
  // if (file) {
  //   // Write some text to the file
  //   size_t written = fwrite(handshake_buff, 1, 68, file);
  //   if (written < 68) {
  //     printf("Warning: Only wrote %zu of 68 bytes.\n", written);
  //   }
  // } else {
  //   perror("fopen");
  // }
  // // Close the file
  // if (file) fclose(file);

  // String handshake_resp = {.data = handshake_buff, .len = strlen(handshake_buff)};
  char url_buff[64] = {0};
  u32 val = peer.ip;
  sprintf(url_buff, "%d.%d.%d.%d",
          (val & 0xFF000000) >> 24,
          (val & 0x00FF0000) >> 16,
          (val & 0x0000FF00) >> 8,
          (val & 0x000000FF) >> 0);

  u32 result = 0;
  u32 s = socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0) {
    printf("socket error: %s\n", strerror(errno));
    return s;
  }

  struct sockaddr_in sock = {
      .sin_addr = {inet_addr(url_buff)},
      .sin_port = htons(peer.port),
      .sin_family = AF_INET,
  };

  u32 c = connect(s, (struct sockaddr *)&sock, sizeof(struct sockaddr_in));
  if (c != 0) {
    printf("connection with peer failed: %s\n", strerror(errno));
    return c;
  }

  if (write(s, handshake_buff, sizeof(handshake_buff)) < 0) {
    printf("failed to send handshake to peer: %s\n", strerror(errno));
    return -1;
  }
  char resp_buff[1024] = {0};
  if (read(s, resp_buff, 1023) < 0) {
    printf("failed to read peer response: %s\n", strerror(errno));
    return -1;
  }
  printf("peer response: %s\n", resp_buff);

  close(s);
  return result;
}
