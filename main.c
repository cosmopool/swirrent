#include <assert.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>

#include "bencode.c"
#define STRING_IMPLEMENTATION
#include "core.h"
#define TORRENT_IMPLEMENTATION
#include "torrent.h"

static String trackers_url[2048] = {0};
static TorrentFile files[2048] = {0};
static TorrentPeer peers[2048] = {0};
static TorrentMetainfo metainfo = {.trackers_url = trackers_url};

// simple write callback
usize write_cb(char *ptr, usize size, usize nmemb, void *userdata) {
  printf("RESPONSE: %s\n", ptr);
  String *r = userdata;
  memcpy((void *)r->data + r->len, ptr, size * nmemb);
  r->len += size * nmemb;
  return size * nmemb;
}

i32 main(i32 argc, char **argv) {
  (void)argc;

  if (!argv[1]) {
    printf("a torrent file must be provided as argument");
    exit(1);
  }
  printf("FILE: %s\n", argv[1]);
  FILE *file_ptr = fopen(argv[1], "rb");
  if (!file_ptr) {
    perror("fopen");
    exit(1);
  }

  // calculate the file size
  fseek(file_ptr, 0, SEEK_END);
  u64 file_length = ftell(file_ptr);
  rewind(file_ptr);

  // allocate and copy the file contents
  char *file_content = (char *)malloc(file_length * sizeof(char));
  fread(file_content, file_length, 1, file_ptr);
  fclose(file_ptr);
  printf("FILE SIZE: %lluK\n", file_length / 1024);

  String torrent_file_content = {
      .len = file_length,
      .data = file_content,
  };

  BencodeParser parser = {0};
  assert(torrent_file_content.data[parser.cursor] == 'd');
  bencodeDecode(&parser, torrent_file_content, &metainfo);

  if (!metainfo.info.is_single_file) {
    metainfo.info.multi_files.files = (TorrentFile *)&files;
  }
  if (argv[2] && memcmp(&argv[2], "-v", 2)) {
    torrentMetainfoPrint(metainfo);
  }
  bencodeInfoDictEncode(metainfo);

  u32 result = 0;
  CURL *curl = curl_easy_init();
  if (!curl) {
    curl_global_cleanup();
    torrentMetainfoCleanup(&metainfo);
    exit(1);
  }

  for (u32 j = 0; j < metainfo.trackers_count; j++) {
    String tracker_url = trackers_url[j];
    if (tracker_url.data[0] == 'u' && tracker_url.data[1] == 'd' &&
        tracker_url.data[2] == 'p') {
      printf("===| Only http trackers are supported. Skipping '%.*s'\n",
             (u32)tracker_url.len, tracker_url.data);
      continue;
    }

    // curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    char url[1024] = {0};

    metainfo.announce = trackers_url[j];
    if (metainfo.announce.len == 0)
      snprintf(url, trackers_url[0].len + 1, "%s", trackers_url[0].data);
    else
      snprintf(url, metainfo.announce.len + 1, "%s", metainfo.announce.data);
    switch (url[metainfo.announce.len - 1]) {
    case '/':
      assert(url[metainfo.announce.len] == '\0');
      url[metainfo.announce.len - 1] = '?';
      break;

    default:
      assert(url[metainfo.announce.len] == '\0');
      url[metainfo.announce.len] = '?';
      break;
    }

    usize offset = 0;
    char encoded_hash[61] = {0};
    for (int i = 0; i < SHA_DIGEST_LENGTH; ++i) {
      offset +=
          sprintf(encoded_hash + offset, "%%%02x", (u8)metainfo.info_hash[i]);
    }
    sprintf(url, "%sinfo_hash=%s", url, encoded_hash);
    sprintf(url, "%s&peer_id=%s", url, "k492jal1dkfj9oa3e8se");
    sprintf(url, "%s&port=%s", url, "6881");
    sprintf(url, "%s&uploaded=%s", url, "0");
    sprintf(url, "%s&downloaded=%s", url, "0");
    if (metainfo.info.is_single_file) {
      sprintf(url, "%s&left=%ld", url, metainfo.info.length);
    } else {
      usize len = 0;
      for (u32 i = 0; i < metainfo.info.multi_files.count; i++) {
        len += metainfo.info.multi_files.files[i].length;
      }
      sprintf(url, "%s&left=%ld", url, len);
    }
    // sprintf(url, "%s&event=%s", url, "empty");
    sprintf(url, "%s&compact=1", url);

    char data[1024] = {0};
    String resp = {.len = 0, .data = data};
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);

    result = curl_easy_perform(curl);
    if (result != CURLE_OK) {
      fprintf(stderr, "===| Communication with tracker '%.*s' failed: %s\n",
              (u32)metainfo.announce.len, metainfo.announce.data,
              curl_easy_strerror(result));
      continue;
    }

    BencodeParser p = {0};
    TorrentTrackerResponse t_resp = {.peers = peers};
    bencodeTrackerResponseDecode(&p, resp, &metainfo, &t_resp);
    if (t_resp.warning_message.len > 0 && t_resp.peer_count == 0) {
      printf(
          "===| Skipping tracker '%.*s' with warning_message: %.*s. Trying another one.\n",
          (u32)metainfo.announce.len, metainfo.announce.data,
          (u32)t_resp.warning_message.len, t_resp.warning_message.data);
      continue;
    }
    if (t_resp.failure_reason.len > 0 && t_resp.peer_count == 0) {
      printf(
          "===| Skipping tracker '%.*s' because failed: %.*s. Trying another one.\n",
          (u32)metainfo.announce.len, metainfo.announce.data,
          (u32)t_resp.failure_reason.len, t_resp.failure_reason.data);
      continue;
    }
    printf("\n===| TRACKER RESPONSE\n");
    printf("tracker url: %.*s\n", (u32)metainfo.announce.len,
           metainfo.announce.data);
    printf("interval: %ld\n", t_resp.interval);
    printf("min interval: %ld\n", t_resp.min_interval);
    printf("complete: %ld\n", t_resp.complete);
    printf("incomplete: %ld\n", t_resp.incomplete);
    printf("downloaded: %ld\n", t_resp.downloaded);
    for (u32 i = 0; i < t_resp.peer_count; i++) {
      if (i == 0) printf("peers:\n");
      u32 val = t_resp.peers[i].ip;
      printf("  (%d)\t ip: %d.%d.%d.%d \t | port: %d\n", i,
             (val & 0xFF000000) >> 24, (val & 0x00FF0000) >> 16,
             (val & 0x0000FF00) >> 8, val & 0x000000FF, t_resp.peers[i].port);
    }
    printf("warning_message: %.*s\n", (u32)t_resp.warning_message.len,
           t_resp.warning_message.data);
    printf("failure_reason: %.*s\n", (u32)t_resp.failure_reason.len,
           t_resp.failure_reason.data);
    break;
  }

  curl_easy_cleanup(curl);
  curl_global_cleanup();
  torrentMetainfoCleanup(&metainfo);
  return (int)result;
}
