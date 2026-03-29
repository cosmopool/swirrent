#pragma once

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../lib/teeny-sha1.c"
#include "bencode.h"
#include "core.h"
#include "torrent.h"

#define IS_LIST parser->bencode[parser->cursor] == 'l'
#define IS_DICT parser->bencode[parser->cursor] == 'd'

#define STRING_MATCHES(key, string)                                 \
  (string.data[0] == (key)[0] && string.len == (sizeof(key) - 1) && \
   memcmp(string.data, key, sizeof(key) - 1) == 0)

BencodeParser bencodeParserFromData(char *data, usize len) {
  BencodeParser parser = {0};
  parser.bencode = data;
  parser.bencode_len = len;
  return parser;
};

BencodeParser bencodeParserFromFile(const char *path) {
  printf("FILE: %s\n", path);
  FILE *file = fopen(path, "rb");
  if (!file) {
    perror("fopen");
    exit(1);
  }

  // calculate the file size
  fseek(file, 0, SEEK_END);
  u64 length = ftell(file);
  rewind(file);

  // allocate and copy the file contents
  char *content = (char *)malloc(length * sizeof(char));
  fread(content, length, 1, file);
  fclose(file);
  printf("FILE SIZE: %luK\n", length / 1024);

  return bencodeParserFromData(content, length);
};

void bencodeParserCleanup(BencodeParser *bencode) {
  free((void *)bencode->bencode);
}

String bencodeStringDecode(BencodeParser *parser) {
  char *colon_ptr;
  errno = 0;
  isize str_len = strtol(&parser->bencode[parser->cursor], &colon_ptr, 10);
  if (errno) {
    char *msg = "[BAD STRING] Not able to decode string lenght: %s\n";
    fprintf(stderr, msg, strerror(errno));
    exit(1);
  }
  if (str_len > 0) assert(parser->bencode_len - parser->cursor > (usize)str_len);

  BencodeValue value = {
      .kind = STRING,
      .string =
          (String){
              .len = str_len,
              .data = colon_ptr + 1,
          },
  };

  // find where the string ended and set the cursor to that position
  parser->cursor = (value.string.data + value.string.len) - parser->bencode;

  return value.string;
}

isize bencodeIntegerDecode(BencodeParser *parser) {
  assert(parser->bencode_len - parser->cursor > 3);
  char *end_ptr;

  const char *start_ptr = &parser->bencode[parser->cursor];
  isize integer = strtol(start_ptr + 1, &end_ptr, 10);
  if (errno) {
    printf("Not able to convert to integer: %s\n", strerror(errno));
    char *msg = "[BAD INTEGER] Not able to convert integer from: %*s\n";
    fprintf(stderr, msg, 25, start_ptr);
    exit(1);
  }

  // find where the string ended and set the cursor to that position
  parser->cursor = end_ptr - parser->bencode + 1;

  return integer;
}

BencodeValue bencodeListDecode(BencodeParser *parser, TorrentMetainfo *metainfo) {
  assert(IS_LIST);
  parser->cursor++;
  usize start = parser->cursor;
  while (parser->bencode[parser->cursor] != 'e') {
    bencodeDecode(parser, metainfo);
  }
  usize end = parser->cursor;
  parser->cursor++;
  BencodeValue value = {0};
  value.kind = LIST;
  value.string = (String){.len = end - start, .data = &parser->bencode[start]};
  return value;
}

BencodeValue bencodeDictDecode(BencodeParser *parser, TorrentMetainfo *metainfo) {
  assert(IS_DICT);
  parser->cursor++;
  usize dict_start = parser->cursor;
  while (parser->bencode[parser->cursor] != 'e') {
    String key = bencodeStringDecode(parser);

    if (STRING_MATCHES("failure reason", key)) {
      String str = bencodeStringDecode(parser);
      printf("failure reason: %.*s\n", (u32)str.len, str.data);
      return (BencodeValue){.kind = STRING, .string = str};
    }

    else if (STRING_MATCHES("announce", key)) {
      metainfo->announce = bencodeStringDecode(parser);
      continue;
    }

    else if (STRING_MATCHES("info", key)) {
      bencodeInfoDictDecode(parser, metainfo);
      continue;
    }

    else if (STRING_MATCHES("url-list", key) ||
             STRING_MATCHES("announce-list", key)) {
      assert(IS_LIST);
      parser->cursor++;
      usize start = parser->cursor;
      while (parser->bencode[parser->cursor] != 'e') {
        if (parser->bencode[parser->cursor] == 'l') {
          parser->cursor++;
          while (parser->bencode[parser->cursor] != 'e') {
            metainfo->trackers_url[metainfo->trackers_count] = bencodeStringDecode(parser);
            metainfo->trackers_count++;
          }
          parser->cursor++;
          continue;
        }
        BencodeValue v = bencodeDecode(parser, metainfo);
        assert(v.kind == STRING);
        metainfo->trackers_url[metainfo->trackers_count] = v.string;
        metainfo->trackers_count++;
      }
      usize end = parser->cursor;
      parser->cursor++;
      BencodeValue value = {0};
      value.kind = LIST;
      value.string = (String){.len = end - start, .data = &parser->bencode[start]};
      continue;
    }

    printf("-> |%.*s\n", (u32)key.len, key.data);
    bencodeDecode(parser, metainfo);
    continue;
  }
  parser->cursor++;
  usize dict_end = parser->cursor;
  BencodeValue value = {0};
  value.kind = DICT;
  value.string = (String){.len = dict_end - dict_start, .data = &parser->bencode[dict_start]};
  return value;
}

BencodeValue bencodeFileDecode(BencodeParser *parser, TorrentInfoFiles *files) {
  assert(IS_DICT);

  usize start = parser->cursor;
  parser->cursor++;
  while (parser->bencode[parser->cursor] != 'e') {
    String key = bencodeStringDecode(parser);

    if (STRING_MATCHES("length", key)) {
      assert(files->files);
      TorrentFile file = {.length = bencodeIntegerDecode(parser)};
      files->files[files->count] = file;
      continue;
    }

    else if (STRING_MATCHES("path", key)) {
      assert(files->files);
      // Record where this file's paths start in the flat array
      usize file_idx = files->count;
      files->files[file_idx].path = &files->paths[parser->path_cursor];

      // Manually consume the list 'l...e' inline, no recursive decode
      assert(IS_LIST);
      parser->cursor++;
      while (parser->bencode[parser->cursor] != 'e') {
        files->paths[parser->path_cursor++] = bencodeStringDecode(parser);
        files->files[file_idx].path_count++;
      }
      parser->cursor++;
      files->count++;
      continue;
    }
  }
  parser->cursor++;
  usize end = parser->cursor;
  BencodeValue value = {0};
  value.kind = DICT;
  value.string = (String){.len = end - start + 1, .data = &parser->bencode[start + 1]};
  return value;
}

BencodeValue bencodeInfoDictDecode(BencodeParser *parser, TorrentMetainfo *metainfo) {
  assert(IS_DICT);

  usize start = parser->cursor;
  parser->cursor++;
  while (parser->bencode[parser->cursor] != 'e') {
    String key = bencodeStringDecode(parser);

    if (STRING_MATCHES("name", key)) {
      metainfo->info.name = bencodeStringDecode(parser);
      continue;
    }

    else if (STRING_MATCHES("piece length", key)) {
      metainfo->info.piece_length = bencodeIntegerDecode(parser);
      continue;
    }

    else if (STRING_MATCHES("pieces", key)) {
      metainfo->info.pieces = bencodeStringDecode(parser);
      continue;
    }

    else if (STRING_MATCHES("length", key)) {
      assert(metainfo->info.multi_files.count == 0);
      assert(!metainfo->info.multi_files.files);
      metainfo->info.is_single_file = true;
      metainfo->info.length = bencodeIntegerDecode(parser);
      continue;
    }

    else if (STRING_MATCHES("files", key)) {
      assert(metainfo->info.length == 0);
      metainfo->info.is_single_file = false;
      torrentInfoMultiFileSet(&metainfo->info);

      assert(IS_LIST);
      parser->cursor++;
      while (parser->bencode[parser->cursor] != 'e') {
        (void)bencodeFileDecode(parser, &metainfo->info.multi_files);
      }
      parser->cursor++;
      continue;
    }

    printf("-> info|%.*s\n", (u32)key.len, key.data);
    bencodeDecode(parser, metainfo);
    continue;
  }
  parser->cursor++;
  usize end = parser->cursor;
  assert(parser->bencode[start] == 'd');
  assert(parser->bencode[end - 1] == 'e');

  BencodeValue value = {0};
  value.kind = DICT;
  value.string = (String){.len = end - start + 1, .data = &parser->bencode[start + 1]};
  return value;
}

BencodeValue bencodeTrackerResponseDecode(BencodeParser *p, TorrentMetainfo *metainfo, TorrentTrackerResponse *resp) {
  if (p->bencode[p->cursor] != 'd') {
    resp->failure_reason = (String){.data = p->bencode, .len = p->bencode_len};
    return (BencodeValue){};
  }

  p->cursor++;
  usize dict_start = p->cursor;
  while (p->bencode[p->cursor] != 'e') {
    String key = bencodeStringDecode(p);

    if (STRING_MATCHES("failure reason", key)) {
      resp->failure_reason = bencodeStringDecode(p);
      return (BencodeValue){.kind = STRING, .string = resp->failure_reason};
    }

    else if (STRING_MATCHES("complete", key)) {
      resp->complete = bencodeIntegerDecode(p);
      continue;
    }

    else if (STRING_MATCHES("downloaded", key)) {
      resp->downloaded = bencodeIntegerDecode(p);
      continue;
    }

    else if (STRING_MATCHES("incomplete", key)) {
      resp->incomplete = bencodeIntegerDecode(p);
      continue;
    }

    else if (STRING_MATCHES("interval", key)) {
      resp->interval = bencodeIntegerDecode(p);
      continue;
    }

    else if (STRING_MATCHES("min interval", key)) {
      resp->min_interval = bencodeIntegerDecode(p);
      continue;
    }

    else if (STRING_MATCHES("peers", key)) {
      if (p->bencode[p->cursor] == 'l') {
        (void)bencodeListDecode(p, metainfo);
        printf("----- decoding peers: only compact form decoding is implemented yet.\n");
        continue;
      }

      String peers_str = bencodeStringDecode(p);
      if (peers_str.len % (IPV4_LEN + PORT_LEN) != 0) {
        printf("----- decoding peers4: invalid length (must be multiple of 10).\n");
        continue;
      }

      resp->peers = peers_str;
      continue;
    }

    else if (STRING_MATCHES("peers6", key)) {
      if (p->bencode[p->cursor] == 'l') {
        (void)bencodeListDecode(p, metainfo);
        printf("----- decoding peers6: only compact form decoding is implemented yet.\n");
        continue;
      }

      String str = bencodeStringDecode(p);
      if (str.len % (IPV6_LEN + PORT_LEN) != 0) {
        printf("----- decoding peers6: invalid length (must be multiple of 18).\n");
        continue;
      }

      resp->peers6 = (TorrentPeers6){
          .data = (char *)str.data,
          .len = str.len,
          .count = str.len / (IPV6_LEN + PORT_LEN),
      };
      continue;
    }

    else if (STRING_MATCHES("warning message", key)) {
      resp->warning_message = bencodeStringDecode(p);
      continue;
    }

    // skip unrecognized/unwanted key/values
    printf("-> |%.*s\n", (u32)key.len, key.data);
    (void)bencodeDecode(p, metainfo);
    continue;
  }
  p->cursor++;
  usize dict_end = p->cursor;
  BencodeValue value = {0};
  value.kind = DICT;
  value.string = (String){.len = dict_end - dict_start, .data = &p->bencode[dict_start]};
  return value;
}

char *bencodeDictKeyEncode(char *key, char *dest) {
  char tmp[128] = {0};
  usize len = strnlen(key, ULONG_MAX);
  i32 encoded_len = snprintf(tmp, 128, "%ld:", len);
  assert(encoded_len > 0);
  memcpy(dest, tmp, encoded_len);
  dest += encoded_len;
  memcpy(dest, key, len);
  return dest + len;
}

char *bencodeIntegerEncode(usize integer, char *dest) {
  char tmp[64] = {0};
  i32 encoded_len = snprintf(tmp, 64, "i%lde", integer);
  assert(encoded_len > 0);
  memcpy(dest, tmp, encoded_len);
  return dest + encoded_len;
}

char *bencodeStringEncode(String string, char *dest) {
  char tmp[64] = {0};
  i32 encoded_len = snprintf(tmp, 64, "%ld:", string.len);
  assert(encoded_len > 0);
  memcpy(dest, tmp, encoded_len);
  dest += encoded_len;
  memcpy(dest, string.data, string.len);
  return dest + string.len;
}

char *bencodeDictEncode(char *dest) {
  dest[0] = 'd';
  return dest + 1;
}

char *bencodeListEncode(char *dest) {
  dest[0] = 'l';
  return dest + 1;
}

char *bencodeDictCloseEncode(char *dest, const char *dict_name) {
  (void)dict_name;
  dest[0] = 'e';
  dest[1] = '\0';
  return dest + 1;
}

char *bencodeListCloseEncode(char *dest, const char *dict_name) {
  (void)dict_name;
  dest[0] = 'e';
  dest[1] = '\0';
  return dest + 1;
}

#define MAX_LEN 2 * 1025 * 1024
void bencodeInfoDictEncode(TorrentMetainfo *metainfo) {
  char buff[MAX_LEN] = {0};
  char *buff_slice = &buff[0];

  buff_slice = bencodeDictEncode(buff_slice);
  if (metainfo->info.is_single_file) {
    buff_slice = bencodeDictKeyEncode("length", buff_slice);
    buff_slice = bencodeIntegerEncode(metainfo->info.length, buff_slice);
  } else {
    buff_slice = bencodeDictKeyEncode("files", buff_slice);
    buff_slice = bencodeListEncode(buff_slice); // files list
    for (u32 i = 0; i < metainfo->info.multi_files.count; i++) {
      buff_slice = bencodeDictEncode(buff_slice); // file dict

      TorrentFile *file = metainfo->info.multi_files.files + i;
      buff_slice = bencodeDictKeyEncode("length", buff_slice);
      buff_slice = bencodeIntegerEncode(file->length, buff_slice);

      buff_slice = bencodeDictKeyEncode("path", buff_slice);
      buff_slice = bencodeListEncode(buff_slice);
      for (u32 j = 0; j < file->path_count; j++) {
        buff_slice = bencodeStringEncode(*(file->path + j), buff_slice);
      }
      buff_slice = bencodeListCloseEncode(buff_slice, "path"); // path list

      buff_slice = bencodeDictCloseEncode(buff_slice, "file"); // file dict
    }
    buff_slice = bencodeListCloseEncode(buff_slice, "files"); // files list
  }

  buff_slice = bencodeDictKeyEncode("name", buff_slice);
  buff_slice = bencodeStringEncode(metainfo->info.name, buff_slice);

  buff_slice = bencodeDictKeyEncode("piece length", buff_slice);
  buff_slice = bencodeIntegerEncode(metainfo->info.piece_length, buff_slice);

  buff_slice = bencodeDictKeyEncode("pieces", buff_slice);
  buff_slice = bencodeStringEncode(metainfo->info.pieces, buff_slice);

  buff_slice = bencodeDictCloseEncode(buff_slice, "info"); // info dict
  usize len = buff_slice - buff;

  if (sha1digest(metainfo->info_hash, NULL, (u8 *)buff, len) != 0) {
    printf("%s:%d Not able to generate the SHA1 hash of 'info' dictionary!", __FILE__, __LINE__);
    exit(1);
  }

  // for (u32 i = 0; i < SHA_DIGEST_LENGTH; ++i) {
  //   sprintf(&metainfo->info_hash[i * 2], "%02x", (u32)hash[i]);
  // }

  printf("SHA1: %s\n", metainfo->info_hash);
}

BencodeValue bencodeDecode(BencodeParser *parser, TorrentMetainfo *metainfo) {
  if (parser->bencode_len <= 0) {
    fprintf(stderr, "Empty data! Nothing to parse.\n");
    exit(1);
  }

  while (parser->cursor < parser->bencode_len) {
    switch (parser->bencode[parser->cursor]) {
    case 'i': {
      isize i = bencodeIntegerDecode(parser);
      return (BencodeValue){.kind = INT, .num = i};
    }

    case '0' ... '9': {
      String s = bencodeStringDecode(parser);
      return (BencodeValue){.kind = STRING, .string = s};
    }

    case 'd': {
      return bencodeDictDecode(parser, metainfo);
    }

    case 'l': {
      return bencodeListDecode(parser, metainfo);
    }
    case 'e':
      parser->cursor++;
      printf("cursor ended at %ld and string length is %ld", parser->cursor, parser->bencode_len);
      break;
    };
  }

  BencodeValue value = {0};
  return value;
}
