#include <assert.h>
#include <curl/curl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "bencode.h"
#define STRING_IMPLEMENTATION
#include "core.h"
#include "teeny-sha1.c"
#define TORRENT_IMPLEMENTATION
#include "torrent.h"

#define IS_LIST bencode.data[parser->cursor] == 'l'
#define IS_DICT bencode.data[parser->cursor] == 'd'

#define STRING_MATCHES(key, string)                                            \
  (string.data[0] == (key)[0] && string.len == (sizeof(key) - 1) &&            \
   memcmp(string.data, key, sizeof(key) - 1) == 0)

#define SHA_DIGEST_LENGTH 20

// static Arena default_arena = {0};
static String trackers_url[2048 * 4] = {0};
static String paths[2048 * 4] = {0};
static TorrentFile files[2048] = {0};
static TorrentPeer peers[2048] = {0};
static TorrentMetainfo metainfo = {0};

void printMetainfo() {
  printf("meta info\n");
  printf("announce: ");
  mcl_printString(metainfo.announce);
  printf("\n");
  printf("name: ");
  mcl_printString(metainfo.info.name);
  printf("\n");
  printf("info hash: %s \n", metainfo.info_hash);
  printf("piece length: %ldK\n", metainfo.info.piece_length / 1024);
  printf("pieces: %lu\n", metainfo.info.pieces.len / 20);
  if (metainfo.info.is_single_file)
    printf("length: %ldM\n", metainfo.info.length / 1024 / 1024);

  for (u32 i = 0; i < metainfo.trackers_count; i++) {
    mcl_printString(trackers_url[i]);
    printf("\n");
  }

  if (!metainfo.info.is_single_file) {
    metainfo.info.multi_files.files = (TorrentFile *)&files;
    if (metainfo.info.multi_files.count > 0) printf("files:\n");
    for (u32 i = 0; i < metainfo.info.multi_files.count; i++) {
      TorrentFile file = files[i];
      printf(" length: %ld\n", file.length);
      printf(" path:\n");
      for (u32 j = 0; j < file.path_count; j++) {
        String path = file.path[j];
        printf("  - %.*s\n", (u32)path.len, path.data);
      }
    }
  }
  printf("\n");
}

void printFromTo(usize start, usize end, const char *str) {
  char buff[1024] = {0};
  for (u32 i = 0; i < end - start; i++) {
    if (str[start + i] == '\0') break;
    buff[i] = str[start + i];
  }
  printf("DEBUG: %s\n", buff);
}

void printStringSlice(usize len, const char *str_c) {
  char buff[1024] = {0};
  String string = {.len = len, .data = str_c};
  for (u32 i = 0; i < string.len; i++) {
    if (string.data[i] == '\0') break;
    buff[i] = string.data[i];
  }

  printf("DEBUG: %s\n", buff);
}

String decodeString(BencodeParser *parser, String bencode) {
  char *colon_ptr;
  errno = 0;
  isize str_len = strtol(&bencode.data[parser->cursor], &colon_ptr, 10);
  if (errno) {
    char *msg = "[BAD STRING] Not able to decode string lenght: %s\n";
    fprintf(stderr, msg, strerror(errno));
    exit(1);
  }
  if (str_len > 0) assert(bencode.len - parser->cursor > (usize)str_len);

  BencodeValue value = {
      .kind = STRING,
      .string =
          (String){
              .len = str_len,
              .data = colon_ptr + 1,
          },
  };

  // find where the string ended and set the cursor to that position
  parser->cursor = (value.string.data + value.string.len) - bencode.data;

  return value.string;
}

isize decodeInteger(BencodeParser *parser, String bencode) {
  assert(bencode.len - parser->cursor > 3);
  char *end_ptr;

  const char *start_ptr = &bencode.data[parser->cursor];
  isize integer = strtol(start_ptr + 1, &end_ptr, 10);
  if (errno) {
    printf("Not able to convert to integer: %s\n", strerror(errno));
    char *msg = "[BAD INTEGER] Not able to convert integer from: %*s\n";
    fprintf(stderr, msg, 25, start_ptr);
    exit(1);
  }

  // find where the string ended and set the cursor to that position
  parser->cursor = end_ptr - bencode.data + 1;

  return integer;
}

BencodeValue decodeList(BencodeParser *parser, String bencode) {
  assert(IS_LIST);
  parser->cursor++;
  usize start = parser->cursor;
  while (bencode.data[parser->cursor] != 'e') {
    BencodeValue v = bencodeDecode(parser, bencode);
    (void)v;
  }
  usize end = parser->cursor;
  parser->cursor++;
  BencodeValue value = {0};
  value.kind = LIST;
  value.string = (String){.len = end - start, .data = &bencode.data[start]};
  return value;
}

BencodeValue decodeDict(BencodeParser *parser, String bencode) {
  assert(IS_DICT);
  parser->cursor++;
  usize dict_start = parser->cursor;
  while (bencode.data[parser->cursor] != 'e') {
    String key = decodeString(parser, bencode);

    if (STRING_MATCHES("failure reason", key)) {
      String str = decodeString(parser, bencode);
      printf("failure reason: %.*s\n", (u32)str.len, str.data);
      return (BencodeValue){.kind = STRING, .string = str};
    }

    if (STRING_MATCHES("announce", key)) {
      metainfo.announce = decodeString(parser, bencode);
      continue;
    }

    if (STRING_MATCHES("info", key)) {
      decodeInfoDict(parser, bencode);
      continue;
    }

    if (STRING_MATCHES("url-list", key) ||
        STRING_MATCHES("announce-list", key)) {
      assert(IS_LIST);
      parser->cursor++;
      usize start = parser->cursor;
      while (bencode.data[parser->cursor] != 'e') {
        if (bencode.data[parser->cursor] == 'l') {
          parser->cursor++;
          while (bencode.data[parser->cursor] != 'e') {
            trackers_url[metainfo.trackers_count] =
                decodeString(parser, bencode);
            metainfo.trackers_count++;
          }
          parser->cursor++;
          continue;
        }
        BencodeValue v = bencodeDecode(parser, bencode);
        assert(v.kind == STRING);
        trackers_url[metainfo.trackers_count] = v.string;
        metainfo.trackers_count++;
      }
      usize end = parser->cursor;
      parser->cursor++;
      BencodeValue value = {0};
      value.kind = LIST;
      value.string = (String){.len = end - start, .data = &bencode.data[start]};
      continue;
    }

    printf("-> |%.*s\n", (u32)key.len, key.data);
    BencodeValue value = bencodeDecode(parser, bencode);
    (void)value;
    continue;
  }
  parser->cursor++;
  usize dict_end = parser->cursor;
  BencodeValue value = {0};
  value.kind = DICT;
  value.string =
      (String){.len = dict_end - dict_start, .data = &bencode.data[dict_start]};
  return value;
};

BencodeValue decodeFile(BencodeParser *parser, String bencode) {
  assert(IS_DICT);

  usize start = parser->cursor;
  parser->cursor++;
  while (bencode.data[parser->cursor] != 'e') {
    String key = decodeString(parser, bencode);

    if (STRING_MATCHES("length", key)) {
      files[metainfo.info.multi_files.count].length =
          decodeInteger(parser, bencode);
      continue;
    }

    if (STRING_MATCHES("path", key)) {
      // Record where this file's paths start in the flat array
      usize file_idx = metainfo.info.multi_files.count;
      files[file_idx].path = &paths[parser->path_cursor];

      // Manually consume the list 'l...e' inline, no recursive decode
      assert(IS_LIST);
      parser->cursor++;
      while (bencode.data[parser->cursor] != 'e') {
        paths[parser->path_cursor++] = decodeString(parser, bencode);
        files[file_idx].path_count++;
      }
      parser->cursor++;
      metainfo.info.multi_files.count++;
      continue;
    }
  }
  parser->cursor++;
  usize end = parser->cursor;
  BencodeValue value = {0};
  value.kind = DICT;
  value.string =
      (String){.len = end - start + 1, .data = &bencode.data[start + 1]};
  return value;
};

BencodeValue decodeInfoDict(BencodeParser *parser, String bencode) {
  assert(IS_DICT);

  usize start = parser->cursor;
  parser->cursor++;
  while (bencode.data[parser->cursor] != 'e') {
    String key = decodeString(parser, bencode);

    if (STRING_MATCHES("name", key)) {
      metainfo.info.name = decodeString(parser, bencode);
      continue;
    }

    if (STRING_MATCHES("piece length", key)) {
      metainfo.info.piece_length = decodeInteger(parser, bencode);
      continue;
    }

    if (STRING_MATCHES("pieces", key)) {
      metainfo.info.pieces = decodeString(parser, bencode);
      continue;
    }

    if (STRING_MATCHES("length", key)) {
      assert(metainfo.info.multi_files.count == 0);
      assert(!metainfo.info.multi_files.files);
      metainfo.info.is_single_file = true;
      metainfo.info.length = decodeInteger(parser, bencode);
      continue;
    }

    if (STRING_MATCHES("files", key)) {
      assert(metainfo.info.length == 0);
      metainfo.info.is_single_file = false;

      assert(IS_LIST);
      parser->cursor++;
      while (bencode.data[parser->cursor] != 'e') {
        (void)decodeFile(parser, bencode);
      }
      parser->cursor++;
      continue;
    }

    printf("-> info|%.*s\n", (u32)key.len, key.data);
    BencodeValue value = bencodeDecode(parser, bencode);
    (void)value;
    continue;
  }
  parser->cursor++;
  usize end = parser->cursor;
  assert(bencode.data[start] == 'd');
  assert(bencode.data[end - 1] == 'e');

  BencodeValue value = {0};
  value.kind = DICT;
  value.string =
      (String){.len = end - start + 1, .data = &bencode.data[start + 1]};
  return value;
};

BencodeValue decodeTrackerResponse(BencodeParser *parser, String bencode,
                                   TorrentTrackerResponse *tracker_resp) {
  assert(IS_DICT);

  parser->cursor++;
  usize dict_start = parser->cursor;
  while (bencode.data[parser->cursor] != 'e') {
    String key = decodeString(parser, bencode);

    if (STRING_MATCHES("failure reason", key)) {
      String str = decodeString(parser, bencode);
      printf("failure reason: %.*s\n", (u32)str.len, str.data);
      return (BencodeValue){.kind = STRING, .string = str};
    }

    if (STRING_MATCHES("complete", key)) {
      tracker_resp->complete = decodeInteger(parser, bencode);
      continue;
    }

    if (STRING_MATCHES("downloaded", key)) {
      tracker_resp->downloaded = decodeInteger(parser, bencode);
      continue;
    }

    if (STRING_MATCHES("incomplete", key)) {
      tracker_resp->incomplete = decodeInteger(parser, bencode);
      continue;
    }

    if (STRING_MATCHES("interval", key)) {
      tracker_resp->interval = decodeInteger(parser, bencode);
      continue;
    }

    if (STRING_MATCHES("min interval", key)) {
      tracker_resp->min_interval = decodeInteger(parser, bencode);
      continue;
    }

    if (STRING_MATCHES("peers6", key)) {
      String peers_str = decodeString(parser, bencode);
      for (u32 i = 0; i < peers_str.len / 6; i++) {
        usize stride = i * 6;
        u32 ip = ((u32)peers_str.data[stride + 0] << 24) +
                 ((u32)peers_str.data[stride + 1] << 16) +
                 ((u32)peers_str.data[stride + 2] << 8) +
                 ((u32)peers_str.data[stride + 3] << 0);
        u16 port = ((u16)peers_str.data[stride + 4] << 8) +
                   ((u16)peers_str.data[stride + 5] << 0);
        tracker_resp->peers[i] = (TorrentPeer){.ip = ip, .port = port};
        tracker_resp->peer_count++;
      }
      continue;
    }

    if (STRING_MATCHES("warning message", key)) {
      tracker_resp->warning_message = decodeString(parser, bencode);
      continue;
    }

    printf("-> |%.*s\n", (u32)key.len, key.data);
    BencodeValue value = bencodeDecode(parser, bencode);
    (void)value;
    continue;
  }
  parser->cursor++;
  usize dict_end = parser->cursor;
  BencodeValue value = {0};
  value.kind = DICT;
  value.string =
      (String){.len = dict_end - dict_start, .data = &bencode.data[dict_start]};
  return value;
};

char *bencodeEncodeDictKey(char *key, char *dest) {
  char tmp[128] = {0};
  usize len = strnlen(key, ULONG_MAX);
  i32 encoded_len = snprintf(tmp, 128, "%ld:", len);
  assert(encoded_len > 0);
  memcpy(dest, tmp, encoded_len);
  dest += encoded_len;
  memcpy(dest, key, len);
  return dest + len;
}

char *bencodeEncodeInteger(usize integer, char *dest) {
  char tmp[64] = {0};
  i32 encoded_len = snprintf(tmp, 64, "i%lde", integer);
  assert(encoded_len > 0);
  memcpy(dest, tmp, encoded_len);
  return dest + encoded_len;
}

char *bencodeEncodeString(String string, char *dest) {
  char tmp[64] = {0};
  i32 encoded_len = snprintf(tmp, 64, "%ld:", string.len);
  assert(encoded_len > 0);
  memcpy(dest, tmp, encoded_len);
  dest += encoded_len;
  memcpy(dest, string.data, string.len);
  return dest + string.len;
}

char *bencodeEncodeDict(char *dest) {
  dest[0] = 'd';
  return dest + 1;
}

char *bencodeEncodeList(char *dest) {
  dest[0] = 'l';
  return dest + 1;
}

char *bencodeEncodeClose(char *dest) {
  dest[0] = 'e';
  dest[1] = '\0';
  return dest + 1;
}

#define MAX_LEN 2 * 1025 * 1024
void bencodeEncodeInfoSHA1(TorrentInfo info) {
  u8 hash[SHA_DIGEST_LENGTH] = {0};
  char buff[MAX_LEN] = {0};
  char *buff_slice = &buff[0];

  buff_slice = bencodeEncodeDict(buff_slice);
  if (info.is_single_file) {
    buff_slice = bencodeEncodeDictKey("length", buff_slice);
    buff_slice = bencodeEncodeInteger(info.length, buff_slice);
  } else {
    buff_slice = bencodeEncodeDictKey("files", buff_slice);
    buff_slice = bencodeEncodeList(buff_slice); // files list
    for (u32 i = 0; i < info.multi_files.count; i++) {
      buff_slice = bencodeEncodeDict(buff_slice); // file dict

      TorrentFile *file = info.multi_files.files + i;
      buff_slice = bencodeEncodeDictKey("length", buff_slice);
      buff_slice = bencodeEncodeInteger(file->length, buff_slice);

      buff_slice = bencodeEncodeDictKey("path", buff_slice);
      buff_slice = bencodeEncodeList(buff_slice);
      for (u32 j = 0; j < file->path_count; j++) {
        buff_slice = bencodeEncodeString(*(file->path + j), buff_slice);
      }
      buff_slice = bencodeEncodeClose(buff_slice); // path list

      buff_slice = bencodeEncodeClose(buff_slice); // file dict
    }
    buff_slice = bencodeEncodeClose(buff_slice); // files list
  }

  buff_slice = bencodeEncodeDictKey("name", buff_slice);
  buff_slice = bencodeEncodeString(info.name, buff_slice);

  buff_slice = bencodeEncodeDictKey("piece length", buff_slice);
  buff_slice = bencodeEncodeInteger(info.piece_length, buff_slice);

  buff_slice = bencodeEncodeDictKey("pieces", buff_slice);
  buff_slice = bencodeEncodeString(info.pieces, buff_slice);

  buff_slice = bencodeEncodeClose(buff_slice); // info dict
  usize len = buff_slice - buff;

  if (sha1digest(hash, NULL, (u8 *)buff, len) != 0) {
    printf("%s:%d Not able to generate the SHA1 hash of 'info' dictionary!",
           __FILE__, __LINE__);
    exit(1);
  }

  for (int i = 0; i < SHA_DIGEST_LENGTH; ++i) {
    sprintf(&metainfo.info_hash[i * 2], "%02x", (u32)hash[i]);
  }

  printf("SHA1: %s\n", metainfo.info_hash);
}

BencodeValue bencodeDecode(BencodeParser *parser, String bencode) {
  if (bencode.len <= 0) {
    fprintf(stderr, "Empty data! Nothing to parse.\n");
    exit(1);
  }

  while (parser->cursor < bencode.len) {
    switch (bencode.data[parser->cursor]) {
    case 'i': {
      isize i = decodeInteger(parser, bencode);
      return (BencodeValue){.kind = INT, .num = i};
    }

    case '0' ... '9': {
      String s = decodeString(parser, bencode);
      return (BencodeValue){.kind = STRING, .string = s};
    }

    case 'd': {
      BencodeValue v = decodeDict(parser, bencode);
      return v;
    }

    case 'l': {
      BencodeValue v = decodeList(parser, bencode);
      return v;
    }
    case 'e':
      parser->cursor++;
      printf("cursor ended at %ld and string length is %ld", parser->cursor,
             bencode.len);
      break;
    };
  }

  BencodeValue value = {0};
  return value;
}

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
  if (file_ptr == NULL) {
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

  String bencode = {
      .len = file_length,
      .data = file_content,
  };

  // printf("HASH: %ld\n", hash("info"));

  BencodeParser parser = {0};
  assert(bencode.data[parser.cursor] == 'd');
  bencodeDecode(&parser, bencode);

  if (!metainfo.info.is_single_file) {
    metainfo.info.multi_files.files = (TorrentFile *)&files;
  }
  if (argv[2] && memcmp(&argv[2], "-v", 2)) {
    printMetainfo();
  }
  bencodeEncodeInfoSHA1(metainfo.info);

  u32 result = 0;
  CURL *curl = curl_easy_init();
  if (curl) {
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

    char url[1024] = {0};
    metainfo.announce = trackers_url[6];
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
    if (result != CURLE_OK)
      fprintf(stderr, "curl_easy_perform() failed: %s\n",
              curl_easy_strerror(result));

    BencodeParser p = {0};
    TorrentTrackerResponse t_resp = {0};
    t_resp.peers = peers;
    decodeTrackerResponse(&p, resp, &t_resp);
    printf("\nTRACKER RESPONSE\n");
    printf("interval: %ld\n", t_resp.interval);
    printf("min interval: %ld\n", t_resp.min_interval);
    printf("complete: %ld\n", t_resp.complete);
    printf("incomplete: %ld\n", t_resp.incomplete);
    printf("downloaded: %ld\n", t_resp.downloaded);
    for (u32 i = 0; i < t_resp.peer_count; i++) {
      if (i == 0) printf("peers:\n");
      printf("  (%d)\t ip: 0x%x \t| port: %d\n", i, t_resp.peers[i].ip,
             t_resp.peers[i].port);
    }
    printf("warning_message: %.*s\n", (u32)t_resp.warning_message.len,
           t_resp.warning_message.data);
    printf("failure_reason: %.*s\n", (u32)t_resp.failure_reason.len,
           t_resp.failure_reason.data);

    curl_easy_cleanup(curl);
  }
  curl_global_cleanup();
  return (int)result;
}
