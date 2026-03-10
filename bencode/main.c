#include <assert.h>
#include <curl/curl.h>
#include <errno.h>
#include <stdio.h>

#include "bencode.h"
#define STRING_IMPLEMENTATION
#include "core.h"
#include "teeny-sha1.c"
#define TORRENT_IMPLEMENTATION
#include "torrent.h"

#define SHA_DIGEST_LENGTH 20

// static Arena default_arena = {0};
static String paths[2048 * 4] = {0};
static TorrentFile files[2048] = {0};
static TorrentMetainfo metainfo = {0};

void printMetainfo() {
  printf("meta info\n");
  printf("announce: ");
  mcl_printString(metainfo.announce);
  printf("\n");
  printf("name: ");
  mcl_printString(metainfo.name);
  printf("\n");
  printf("info hash: %s \n", metainfo.info_hash);
  if (metainfo.is_single_file) {
    printf("length: %ldM\n", metainfo.single_file.length / 1024 / 1024);
  } else {
    metainfo.multi_file.files = (TorrentFile *)&files;
    if (metainfo.multi_file.file_count > 0) printf("files:\n");
    for (u32 i = 0; i < metainfo.multi_file.file_count; i++) {
      TorrentFile file = files[i];
      printf(" length: %ld\n", file.length);
      printf(" path:\n");
      for (u32 j = 0; j < file.path_count; j++) {
        String path = file.path[j];
        printf("  - %.*s\n", (u32)path.len, path.data);
      }
    }
  }
  printf("piece length: %ldK\n", metainfo.piece_length / 1024);
  printf("pieces: %lu", metainfo.pieces.len / 20);
  printf("\n");
}

void printFromTo(usize start, usize end, const char *str) {
  char buff[1024] = {0};
  for (i32 i = 0; i < end - start; i++) {
    if (str[start + i] == '\0') break;
    buff[i] = str[start + i];
  }
  printf("DEBUG: %s\n", buff);
}

void printStringSlice(usize len, const char *str_c) {
  char buff[1024] = {0};
  String string = {.len = len, .data = str_c};
  for (i32 i = 0; i < string.len; i++) {
    if (string.data[i] == '\0') break;
    buff[i] = string.data[i];
  }

  printf("DEBUG: %s\n", buff);
}

String parseString(BencodeParser *parser, String bencode) {
  char *colon_ptr;
  errno = 0;
  isize str_len = strtol(&bencode.data[parser->cursor], &colon_ptr, 10);
  if (errno) {
    char *msg = "[BAD STRING] Not able to decode string lenght: %s\n";
    fprintf(stderr, msg, strerror(errno));
    exit(1);
  }
  assert(bencode.len - parser->cursor > str_len);

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

isize parseInteger(BencodeParser *parser, String bencode) {
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

BencodeValue parseList(BencodeParser *parser, String bencode,
                       const char *dict_path) {
  parser->cursor++;
  usize start = parser->cursor;
  // printf("LIST: [");
  while (bencode.data[parser->cursor] != 'e') {
    BencodeValue v = bencode_decode(parser, bencode, dict_path);
    (void)v;
    // switch (v.kind) {
    // case STRING:
    // case LIST:
    // case DICT:
    //   printf("%.*s,", (u32)v.string.len, v.string.data);
    //   break;
    // case INT:
    //   printf("%lld,", v.num);
    //   break;
    // }
  }
  // printf("]");
  usize end = parser->cursor;
  parser->cursor++;
  BencodeValue value = {0};
  value.kind = LIST;
  value.string = (String){.len = end - start, .data = &bencode.data[start]};
  return value;
}

BencodeValue parseDict(BencodeParser *parser, String bencode,
                       const char *dict_path) {
  parser->cursor++;
  usize dict_start = parser->cursor;
  while (bencode.data[parser->cursor] != 'e') {
    String key = parseString(parser, bencode);
    bool is_at_root = dict_path[0] == '\0';

    if (is_at_root && key.data[0] == 'f' && key.len == 14 &&
        memcmp(key.data, "failure reason", 14) == 0) {
      BencodeValue value = bencode_decode(parser, bencode, dict_path);
      assert(value.kind == STRING);
      printf("failure reason: %.*s\n", (u32)value.string.len,
             value.string.data);
      return value;
    }

    if (is_at_root && key.data[0] == 'a' && key.len == 13 &&
        memcmp(key.data, "announce-list", 13) == 0) {
      BencodeValue v = bencode_decode(parser, bencode, dict_path);
      (void)v;
      continue;
    }

    if (is_at_root && key.data[0] == 'a' && key.len == 8 &&
        memcmp(key.data, "announce", 8) == 0) {
      metainfo.announce = parseString(parser, bencode);
      continue;
    }

    if (dict_path[0] == 'i' && key.data[0] == 'n' && key.len == 4 &&
        memcmp(key.data, "name", 4) == 0 &&
        memcmp(dict_path, "info", 10) == 0) {
      metainfo.name = parseString(parser, bencode);
      continue;
    }

    if (dict_path[0] == 'i' && key.data[0] == 'p' && key.len == 12 &&
        memcmp(key.data, "piece length", 12) == 0 &&
        memcmp(dict_path, "info", 10) == 0) {
      metainfo.piece_length = parseInteger(parser, bencode);
      continue;
    }

    if (dict_path[0] == 'i' && key.data[0] == 'p' && key.len == 6 &&
        memcmp(key.data, "pieces", 6) == 0 &&
        memcmp(dict_path, "info", 10) == 0) {
      metainfo.pieces = parseString(parser, bencode);
      continue;
    }

    if (dict_path[0] == 'i' && key.data[0] == 'l' && key.len == 6 &&
        memcmp(key.data, "length", 6) == 0 &&
        memcmp(dict_path, "info", 10) == 0) {
      metainfo.is_single_file = true;
      metainfo.single_file.length = parseInteger(parser, bencode);
      continue;
    }

    if (dict_path[0] == 'i' && key.data[0] == 'l' && key.len == 6 &&
        memcmp(key.data, "length", 6) == 0 &&
        memcmp(dict_path, "info|files", 10) == 0) {
      metainfo.is_single_file = false;
      BencodeValue value = bencode_decode(parser, bencode, dict_path);
      files[metainfo.multi_file.file_count].length = value.num;
      continue;
    }

    if (dict_path[0] == 'i' && key.data[0] == 'p' && key.len == 4 &&
        memcmp(key.data, "path", 4) == 0 &&
        memcmp(dict_path, "info|files", 10) == 0) {
      metainfo.is_single_file = false;

      // Record where this file's paths start in the flat array
      usize file_idx = metainfo.multi_file.file_count;
      files[file_idx].path = &paths[parser->path_cursor];

      // Manually consume the list 'l...e' inline, no recursive decode
      assert(bencode.data[parser->cursor] == 'l');
      parser->cursor++;
      while (bencode.data[parser->cursor] != 'e') {
        paths[parser->path_cursor++] = parseString(parser, bencode);
        files[file_idx].path_count++;
      }
      parser->cursor++;
      metainfo.multi_file.file_count++;
      continue;
    }

    if (is_at_root && key.data[0] == 'i' && key.len == 4 &&
        memcmp(key.data, "info", 4) == 0) {
      u8 hash[SHA_DIGEST_LENGTH];
      usize start = parser->cursor;
      bencode_decode(parser, bencode, "info");
      usize end = parser->cursor - 1;
      assert(bencode.data[start] == 'd');
      assert(bencode.data[end] == 'e');
      if (sha1digest(hash, NULL, (u8 *)&bencode.data[start], end - start) != 0)
        exit(1);

      for (int i = 0; i < SHA_DIGEST_LENGTH; ++i) {
        sprintf(&metainfo.info_hash[i * 2], "%02x", (u32)hash[i]);
      }
      continue;
    }

    char new_key_path[128] = {0};
    usize len = strlen(dict_path);
    snprintf(new_key_path, len + 1 + key.len + 1, "%s|%s", dict_path, key.data);
    printf("-> %s\n", new_key_path);

    BencodeValue value = bencode_decode(parser, bencode, new_key_path);
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

BencodeValue bencode_decode(BencodeParser *parser, String bencode,
                            const char *dict_path) {
  if (bencode.len <= 0) {
    fprintf(stderr, "Empty data! Nothing to parse.\n");
    exit(1);
  }

  while (parser->cursor < bencode.len) {
    switch (bencode.data[parser->cursor]) {
    case 'i': {
      isize i = parseInteger(parser, bencode);
      return (BencodeValue){.kind = INT, .num = i};
    }

    case '0' ... '9': {
      String s = parseString(parser, bencode);
      return (BencodeValue){.kind = STRING, .string = s};
    }

    case 'd': {
      BencodeValue v = parseDict(parser, bencode, dict_path);
      return v;
    }

    case 'l': {
      BencodeValue v = parseList(parser, bencode, dict_path);
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
  // printf("RESPONSE: %s\n", ptr);
  String *r = userdata;
  memcpy((void *)r->data + r->len, ptr, size * nmemb);
  r->len += size * nmemb;
  return size * nmemb;
}

i32 main(i32 argc, char **argv) {

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
  bencode_decode(&parser, bencode, "");

  if (!metainfo.is_single_file) {
    metainfo.multi_file.files = (TorrentFile *)&files;
  }
  if (argv[2] && memcmp(&argv[2], "-v", 2)) {
    printMetainfo();
  }

  u32 result = 0;
  CURL *curl = curl_easy_init();
  if (curl) {
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

    char url[1024] = {0};
    snprintf(url, metainfo.announce.len + 1, "%s", metainfo.announce.data);
    if (url[metainfo.announce.len] == '\0') url[metainfo.announce.len] = '?';
    char encoded_hash[61] = {0};
    usize offset = 0;
    for (int i = 0; i < SHA_DIGEST_LENGTH; ++i) {
      offset +=
          sprintf(encoded_hash + offset, "%%%02x", (u8)metainfo.info_hash[i]);
    }
    sprintf(url, "%sinfo_hash=%s", url, encoded_hash);
    sprintf(url, "%s&peer_id=%s", url, "k492jal1dkfj9oa3e8se");
    sprintf(url, "%s&port=%s", url, "6881");
    sprintf(url, "%s&uploaded=%s", url, "0");
    sprintf(url, "%s&downloaded=%s", url, "0");
    if (metainfo.is_single_file) {
      sprintf(url, "%s&left=%ld", url, metainfo.single_file.length);
    } else {
      usize len = 0;
      for (u32 i = 0; i < metainfo.multi_file.file_count; i++) {
        len += metainfo.multi_file.files[i].length;
      }
      sprintf(url, "%s&left=%ld", url, len);
    }
    sprintf(url, "%s&event=%s", url, "empty");

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
    bencode_decode(&p, resp, "");

    curl_easy_cleanup(curl);
  }
  curl_global_cleanup();
  return (int)result;
}
