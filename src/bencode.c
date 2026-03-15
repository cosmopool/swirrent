#pragma once

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "../lib/teeny-sha1.c"
#include "bencode.h"
#include "core.h"
#include "torrent.h"

#define IS_LIST bencode.data[parser->cursor] == 'l'
#define IS_DICT bencode.data[parser->cursor] == 'd'

#define STRING_MATCHES(key, string)                                 \
  (string.data[0] == (key)[0] && string.len == (sizeof(key) - 1) && \
   memcmp(string.data, key, sizeof(key) - 1) == 0)

String bencodeStringDecode(BencodeParser *parser, String bencode) {
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

isize bencodeIntegerDecode(BencodeParser *parser, String bencode) {
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

BencodeValue bencodeListDecode(BencodeParser *parser, String bencode, TorrentMetainfo *metainfo) {
  assert(IS_LIST);
  parser->cursor++;
  usize start = parser->cursor;
  while (bencode.data[parser->cursor] != 'e') {
    bencodeDecode(parser, bencode, metainfo);
  }
  usize end = parser->cursor;
  parser->cursor++;
  BencodeValue value = {0};
  value.kind = LIST;
  value.string = (String){.len = end - start, .data = &bencode.data[start]};
  return value;
}

BencodeValue bencodeDictDecode(BencodeParser *parser, String bencode, TorrentMetainfo *metainfo) {
  assert(IS_DICT);
  parser->cursor++;
  usize dict_start = parser->cursor;
  while (bencode.data[parser->cursor] != 'e') {
    String key = bencodeStringDecode(parser, bencode);

    if (STRING_MATCHES("failure reason", key)) {
      String str = bencodeStringDecode(parser, bencode);
      printf("failure reason: %.*s\n", (u32)str.len, str.data);
      return (BencodeValue){.kind = STRING, .string = str};
    }

    if (STRING_MATCHES("announce", key)) {
      metainfo->announce = bencodeStringDecode(parser, bencode);
      continue;
    }

    if (STRING_MATCHES("info", key)) {
      bencodeInfoDictDecode(parser, bencode, metainfo);
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
            metainfo->trackers_url[metainfo->trackers_count] = bencodeStringDecode(parser, bencode);
            metainfo->trackers_count++;
          }
          parser->cursor++;
          continue;
        }
        BencodeValue v = bencodeDecode(parser, bencode, metainfo);
        assert(v.kind == STRING);
        metainfo->trackers_url[metainfo->trackers_count] = v.string;
        metainfo->trackers_count++;
      }
      usize end = parser->cursor;
      parser->cursor++;
      BencodeValue value = {0};
      value.kind = LIST;
      value.string = (String){.len = end - start, .data = &bencode.data[start]};
      continue;
    }

    printf("-> |%.*s\n", (u32)key.len, key.data);
    bencodeDecode(parser, bencode, metainfo);
    continue;
  }
  parser->cursor++;
  usize dict_end = parser->cursor;
  BencodeValue value = {0};
  value.kind = DICT;
  value.string = (String){.len = dict_end - dict_start, .data = &bencode.data[dict_start]};
  return value;
}

BencodeValue bencodeFileDecode(BencodeParser *parser, String bencode, TorrentInfoFiles *files) {
  assert(IS_DICT);

  usize start = parser->cursor;
  parser->cursor++;
  while (bencode.data[parser->cursor] != 'e') {
    String key = bencodeStringDecode(parser, bencode);

    if (STRING_MATCHES("length", key)) {
      assert(files->files);
      TorrentFile file = {.length = bencodeIntegerDecode(parser, bencode)};
      files->files[files->count] = file;
      continue;
    }

    if (STRING_MATCHES("path", key)) {
      assert(files->files);
      // Record where this file's paths start in the flat array
      usize file_idx = files->count;
      files->files[file_idx].path = &files->paths[parser->path_cursor];

      // Manually consume the list 'l...e' inline, no recursive decode
      assert(IS_LIST);
      parser->cursor++;
      while (bencode.data[parser->cursor] != 'e') {
        files->paths[parser->path_cursor++] = bencodeStringDecode(parser, bencode);
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
  value.string = (String){.len = end - start + 1, .data = &bencode.data[start + 1]};
  return value;
}

BencodeValue bencodeInfoDictDecode(BencodeParser *parser, String bencode, TorrentMetainfo *metainfo) {
  assert(IS_DICT);

  usize start = parser->cursor;
  parser->cursor++;
  while (bencode.data[parser->cursor] != 'e') {
    String key = bencodeStringDecode(parser, bencode);

    if (STRING_MATCHES("name", key)) {
      metainfo->info.name = bencodeStringDecode(parser, bencode);
      continue;
    }

    if (STRING_MATCHES("piece length", key)) {
      metainfo->info.piece_length = bencodeIntegerDecode(parser, bencode);
      continue;
    }

    if (STRING_MATCHES("pieces", key)) {
      metainfo->info.pieces = bencodeStringDecode(parser, bencode);
      continue;
    }

    if (STRING_MATCHES("length", key)) {
      assert(metainfo->info.multi_files.count == 0);
      assert(!metainfo->info.multi_files.files);
      metainfo->info.is_single_file = true;
      metainfo->info.length = bencodeIntegerDecode(parser, bencode);
      continue;
    }

    if (STRING_MATCHES("files", key)) {
      assert(metainfo->info.length == 0);
      metainfo->info.is_single_file = false;
      torrentInfoMultiFileSet(&metainfo->info);

      assert(IS_LIST);
      parser->cursor++;
      while (bencode.data[parser->cursor] != 'e') {
        (void)bencodeFileDecode(parser, bencode, &metainfo->info.multi_files);
      }
      parser->cursor++;
      continue;
    }

    printf("-> info|%.*s\n", (u32)key.len, key.data);
    bencodeDecode(parser, bencode, metainfo);
    continue;
  }
  parser->cursor++;
  usize end = parser->cursor;
  assert(bencode.data[start] == 'd');
  assert(bencode.data[end - 1] == 'e');

  BencodeValue value = {0};
  value.kind = DICT;
  value.string = (String){.len = end - start + 1, .data = &bencode.data[start + 1]};
  return value;
}

BencodeValue bencodeTrackerResponseDecode(BencodeParser *p, String bencode, TorrentMetainfo *metainfo, TorrentTrackerResponse *resp) {
  if (bencode.data[p->cursor] != 'd') {
    resp->failure_reason = bencode;
    return (BencodeValue){};
  }

  p->cursor++;
  usize dict_start = p->cursor;
  while (bencode.data[p->cursor] != 'e') {
    String key = bencodeStringDecode(p, bencode);

    if (STRING_MATCHES("failure reason", key)) {
      resp->failure_reason = bencodeStringDecode(p, bencode);
      return (BencodeValue){.kind = STRING, .string = resp->failure_reason};
    }

    if (STRING_MATCHES("complete", key)) {
      resp->complete = bencodeIntegerDecode(p, bencode);
      continue;
    }

    if (STRING_MATCHES("downloaded", key)) {
      resp->downloaded = bencodeIntegerDecode(p, bencode);
      continue;
    }

    if (STRING_MATCHES("incomplete", key)) {
      resp->incomplete = bencodeIntegerDecode(p, bencode);
      continue;
    }

    if (STRING_MATCHES("interval", key)) {
      resp->interval = bencodeIntegerDecode(p, bencode);
      continue;
    }

    if (STRING_MATCHES("min interval", key)) {
      resp->min_interval = bencodeIntegerDecode(p, bencode);
      continue;
    }

    if (STRING_MATCHES("peers6", key)) {
      String peers_str = bencodeStringDecode(p, bencode);
      for (u32 i = 0; i < peers_str.len / 6; i++) {
        usize stride = i * 6;
        u32 ip = ((u32)peers_str.data[stride + 0] << 24) +
                 ((u32)peers_str.data[stride + 1] << 16) +
                 ((u32)peers_str.data[stride + 2] << 8) +
                 ((u32)peers_str.data[stride + 3] << 0);
        u16 port = ((u16)peers_str.data[stride + 4] << 8) +
                   ((u16)peers_str.data[stride + 5] << 0);
        resp->peers[i] = (TorrentPeer){.ip = ip, .port = port};
        resp->peer_count++;
      }
      continue;
    }

    if (STRING_MATCHES("warning message", key)) {
      resp->warning_message = bencodeStringDecode(p, bencode);
      continue;
    }

    printf("-> |%.*s\n", (u32)key.len, key.data);
    bencodeDecode(p, bencode, metainfo);
    continue;
  }
  p->cursor++;
  usize dict_end = p->cursor;
  BencodeValue value = {0};
  value.kind = DICT;
  value.string = (String){.len = dict_end - dict_start, .data = &bencode.data[dict_start]};
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
void bencodeInfoDictEncode(TorrentMetainfo metainfo) {
  u8 hash[SHA_DIGEST_LENGTH] = {0};
  char buff[MAX_LEN] = {0};
  char *buff_slice = &buff[0];

  buff_slice = bencodeDictEncode(buff_slice);
  if (metainfo.info.is_single_file) {
    buff_slice = bencodeDictKeyEncode("length", buff_slice);
    buff_slice = bencodeIntegerEncode(metainfo.info.length, buff_slice);
  } else {
    buff_slice = bencodeDictKeyEncode("files", buff_slice);
    buff_slice = bencodeListEncode(buff_slice); // files list
    for (u32 i = 0; i < metainfo.info.multi_files.count; i++) {
      buff_slice = bencodeDictEncode(buff_slice); // file dict

      TorrentFile *file = metainfo.info.multi_files.files + i;
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
  buff_slice = bencodeStringEncode(metainfo.info.name, buff_slice);

  buff_slice = bencodeDictKeyEncode("piece length", buff_slice);
  buff_slice = bencodeIntegerEncode(metainfo.info.piece_length, buff_slice);

  buff_slice = bencodeDictKeyEncode("pieces", buff_slice);
  buff_slice = bencodeStringEncode(metainfo.info.pieces, buff_slice);

  buff_slice = bencodeDictCloseEncode(buff_slice, "info"); // info dict
  usize len = buff_slice - buff;

  if (sha1digest(hash, NULL, (u8 *)buff, len) != 0) {
    printf("%s:%d Not able to generate the SHA1 hash of 'info' dictionary!", __FILE__, __LINE__);
    exit(1);
  }

  for (u32 i = 0; i < SHA_DIGEST_LENGTH; ++i) {
    sprintf(&metainfo.info_hash[i * 2], "%02x", (u32)hash[i]);
  }

  printf("SHA1: %s\n", metainfo.info_hash);
}

BencodeValue bencodeDecode(BencodeParser *parser, String bencode, TorrentMetainfo *metainfo) {
  if (bencode.len <= 0) {
    fprintf(stderr, "Empty data! Nothing to parse.\n");
    exit(1);
  }

  while (parser->cursor < bencode.len) {
    switch (bencode.data[parser->cursor]) {
    case 'i': {
      isize i = bencodeIntegerDecode(parser, bencode);
      return (BencodeValue){.kind = INT, .num = i};
    }

    case '0' ... '9': {
      String s = bencodeStringDecode(parser, bencode);
      return (BencodeValue){.kind = STRING, .string = s};
    }

    case 'd': {
      return bencodeDictDecode(parser, bencode, metainfo);
    }

    case 'l': {
      return bencodeListDecode(parser, bencode, metainfo);
    }
    case 'e':
      parser->cursor++;
      printf("cursor ended at %ld and string length is %ld", parser->cursor, bencode.len);
      break;
    };
  }

  BencodeValue value = {0};
  return value;
}
