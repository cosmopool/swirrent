#include "bencode.h"
#include "torrent.h"
#include <curl/curl.h>
#include <stdio.h>
#include <string.h>

#define STRING_IMPLEMENTATION
#include "core.h"
#include "downloader.h"

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

    result = curl_easy_perform(curl);
    if (result != CURLE_OK) {
      fprintf(stderr, "----- Communication with tracker an error occurred: %s\n", curl_easy_strerror(result));
      continue;
    }

    TorrentTrackerResponse t_resp = {.peers = peers};
    u32 tracker_result = downloaderTrackerResponseDecode(resp, metainfo, &t_resp);
    if (tracker_result != 0) continue;
    break;
  }

  curl_easy_cleanup(curl);
  curl_global_cleanup();
  return result;
}
