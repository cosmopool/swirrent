#pragma once
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "bencode.h"
#include "core.h"
#include "teeny-sha1.c"
#include "torrent.h"

#define IS_LIST bencode.data[parser->cursor] == 'l'
#define IS_DICT bencode.data[parser->cursor] == 'd'

#define STRING_MATCHES(key, string)                                            \
  (string.data[0] == (key)[0] && string.len == (sizeof(key) - 1) &&            \
   memcmp(string.data, key, sizeof(key) - 1) == 0)

#define SHA_DIGEST_LENGTH 20

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

BencodeValue decodeList(BencodeParser *parser, String bencode,
                        TorrentMetainfo *metainfo) {
  assert(IS_LIST);
  parser->cursor++;
  usize start = parser->cursor;
  while (bencode.data[parser->cursor] != 'e') {
    BencodeValue v = bencodeDecode(parser, bencode, metainfo);
    (void)v;
  }
  usize end = parser->cursor;
  parser->cursor++;
  BencodeValue value = {0};
  value.kind = LIST;
  value.string = (String){.len = end - start, .data = &bencode.data[start]};
  return value;
}

BencodeValue decodeDict(BencodeParser *parser, String bencode,
                        TorrentMetainfo *metainfo) {
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
      metainfo->announce = decodeString(parser, bencode);
      continue;
    }

    if (STRING_MATCHES("info", key)) {
      decodeInfoDict(parser, bencode, metainfo);
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
            metainfo->trackers_url[metainfo->trackers_count] =
                decodeString(parser, bencode);
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
    BencodeValue value = bencodeDecode(parser, bencode, metainfo);
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

BencodeValue decodeFile(BencodeParser *parser, String bencode,
                        TorrentInfoFiles *files) {
  assert(IS_DICT);

  usize start = parser->cursor;
  parser->cursor++;
  while (bencode.data[parser->cursor] != 'e') {
    String key = decodeString(parser, bencode);

    if (STRING_MATCHES("length", key)) {
      assert(files->files);
      TorrentFile file = {.length = decodeInteger(parser, bencode)};
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
        files->paths[parser->path_cursor++] = decodeString(parser, bencode);
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
  value.string =
      (String){.len = end - start + 1, .data = &bencode.data[start + 1]};
  return value;
};

BencodeValue decodeInfoDict(BencodeParser *parser, String bencode,
                            TorrentMetainfo *metainfo) {
  assert(IS_DICT);

  usize start = parser->cursor;
  parser->cursor++;
  while (bencode.data[parser->cursor] != 'e') {
    String key = decodeString(parser, bencode);

    if (STRING_MATCHES("name", key)) {
      metainfo->info.name = decodeString(parser, bencode);
      continue;
    }

    if (STRING_MATCHES("piece length", key)) {
      metainfo->info.piece_length = decodeInteger(parser, bencode);
      continue;
    }

    if (STRING_MATCHES("pieces", key)) {
      metainfo->info.pieces = decodeString(parser, bencode);
      continue;
    }

    if (STRING_MATCHES("length", key)) {
      assert(metainfo->info.multi_files.count == 0);
      assert(!metainfo->info.multi_files.files);
      metainfo->info.is_single_file = true;
      metainfo->info.length = decodeInteger(parser, bencode);
      continue;
    }

    if (STRING_MATCHES("files", key)) {
      assert(metainfo->info.length == 0);
      metainfo->info.is_single_file = false;
      Torrent_InfoMultiFileSet(&metainfo->info);

      assert(IS_LIST);
      parser->cursor++;
      while (bencode.data[parser->cursor] != 'e') {
        (void)decodeFile(parser, bencode, &metainfo->info.multi_files);
      }
      parser->cursor++;
      continue;
    }

    printf("-> info|%.*s\n", (u32)key.len, key.data);
    BencodeValue value = bencodeDecode(parser, bencode, metainfo);
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
                                   TorrentMetainfo *metainfo,
                                   TorrentTrackerResponse *tracker_resp) {
  if (bencode.data[parser->cursor] != 'd') {
    tracker_resp->failure_reason = bencode;
    return (BencodeValue){};
  }

  parser->cursor++;
  usize dict_start = parser->cursor;
  while (bencode.data[parser->cursor] != 'e') {
    String key = decodeString(parser, bencode);

    if (STRING_MATCHES("failure reason", key)) {
      tracker_resp->failure_reason = decodeString(parser, bencode);
      return (BencodeValue){.kind = STRING,
                            .string = tracker_resp->failure_reason};
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
    BencodeValue value = bencodeDecode(parser, bencode, metainfo);
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
void bencodeEncodeInfoSHA1(TorrentMetainfo metainfo) {
  u8 hash[SHA_DIGEST_LENGTH] = {0};
  char buff[MAX_LEN] = {0};
  char *buff_slice = &buff[0];

  buff_slice = bencodeEncodeDict(buff_slice);
  if (metainfo.info.is_single_file) {
    buff_slice = bencodeEncodeDictKey("length", buff_slice);
    buff_slice = bencodeEncodeInteger(metainfo.info.length, buff_slice);
  } else {
    buff_slice = bencodeEncodeDictKey("files", buff_slice);
    buff_slice = bencodeEncodeList(buff_slice); // files list
    for (u32 i = 0; i < metainfo.info.multi_files.count; i++) {
      buff_slice = bencodeEncodeDict(buff_slice); // file dict

      TorrentFile *file = metainfo.info.multi_files.files + i;
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
  buff_slice = bencodeEncodeString(metainfo.info.name, buff_slice);

  buff_slice = bencodeEncodeDictKey("piece length", buff_slice);
  buff_slice = bencodeEncodeInteger(metainfo.info.piece_length, buff_slice);

  buff_slice = bencodeEncodeDictKey("pieces", buff_slice);
  buff_slice = bencodeEncodeString(metainfo.info.pieces, buff_slice);

  buff_slice = bencodeEncodeClose(buff_slice); // info dict
  usize len = buff_slice - buff;

  if (sha1digest(hash, NULL, (u8 *)buff, len) != 0) {
    printf("%s:%d Not able to generate the SHA1 hash of 'info' dictionary!",
           __FILE__, __LINE__);
    exit(1);
  }

  for (u32 i = 0; i < SHA_DIGEST_LENGTH; ++i) {
    sprintf(&metainfo.info_hash[i * 2], "%02x", (u32)hash[i]);
  }

  printf("SHA1: %s\n", metainfo.info_hash);
}

BencodeValue bencodeDecode(BencodeParser *parser, String bencode,
                           TorrentMetainfo *metainfo) {
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
      BencodeValue v = decodeDict(parser, bencode, metainfo);
      return v;
    }

    case 'l': {
      BencodeValue v = decodeList(parser, bencode, metainfo);
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
