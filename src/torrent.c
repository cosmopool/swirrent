#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../lib/teeny-sha1.c"
#include "bencode.h"
#include "core.h"
#include "torrent.h"

void torrentPieceHashGet(usize piece_idx, TorrentMetainfo *metainfo, char *hash) {
  usize real_idx = piece_idx * 20;
  for (u32 i = 0; i < 20; i += 4) {
    hash[i + 0] = metainfo->info.pieces.data[real_idx + i + 0];
    hash[i + 1] = metainfo->info.pieces.data[real_idx + i + 1];
    hash[i + 2] = metainfo->info.pieces.data[real_idx + i + 2];
    hash[i + 3] = metainfo->info.pieces.data[real_idx + i + 3];
  }
}

void torrentInfoMultiFileSet(TorrentInfo *info) {
  if (info->is_single_file) return;

  if (!info->multi_files.files) {
    usize size = sizeof(TorrentFile) * 2048;
    info->multi_files.files = malloc(size);
    assert(info->multi_files.files);
  }

  if (!info->multi_files.paths) {
    usize size = sizeof(String) * 2048;
    info->multi_files.paths = malloc(size);
    assert(info->multi_files.paths);
  }
}

TorrentMetainfo *torrentMetainfoInit() {
  TorrentMetainfo *metainfo = malloc(sizeof(TorrentMetainfo));
  memset(metainfo, 0, sizeof(TorrentMetainfo));
  metainfo->trackers_url = malloc(sizeof(String) * 2048);
  memset(metainfo->trackers_url, 0, sizeof(String) * 2048);
  return metainfo;
}

void torrentMetainfoCleanup(TorrentMetainfo *mi) {
  assert(mi->trackers_url);
  free(mi->trackers_url);

  if (mi->info.is_single_file) {
    assert(!mi->info.multi_files.files);
    assert(!mi->info.multi_files.paths);
    return;
  }

  assert(mi->info.multi_files.files);
  free(mi->info.multi_files.files);

  assert(mi->info.multi_files.paths);
  free(mi->info.multi_files.paths);

  free(mi);
}

void torrentMetainfoPrint(TorrentMetainfo metainfo) {
  printf("meta info\n");
  printf("announce: ");
  mclPrintString(metainfo.announce);
  printf("\n");
  printf("name: ");
  mclPrintString(metainfo.info.name);
  printf("\n");
  printf("info hash: %s \n", metainfo.info_hash);
  printf("piece length: %ldK\n", metainfo.info.piece_length / 1024);
  printf("pieces: %lu\n", metainfo.info.pieces.len / 20);
  if (metainfo.info.is_single_file)
    printf("length: %ldM\n", metainfo.info.length / 1024 / 1024);

  for (u32 i = 0; i < metainfo.trackers_count; i++) {
    mclPrintString(metainfo.trackers_url[i]);
    printf("\n");
  }

  if (!metainfo.info.is_single_file) {
    if (metainfo.info.multi_files.count > 0) printf("files:\n");
    for (u32 i = 0; i < metainfo.info.multi_files.count; i++) {
      TorrentFile file = metainfo.info.multi_files.files[i];
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

TorrentPeer torrentPeerGet(const char *peers, usize idx) {
  const char *entry = peers + idx * (IPV4_LEN + PORT_LEN);
  return (TorrentPeer){
      .ip = {
          .data = (const char *)entry,
          .len = IPV4_LEN,
      },
      .port = ((u8)entry[IPV4_LEN] << 8) | (u8)entry[IPV4_LEN + 1],
  };
}

TorrentPeer6 torrentPeer6Get(const char *peers, usize idx) {
  const char *entry = peers + idx * (IPV6_LEN + PORT_LEN);
  return (TorrentPeer6){
      .ip = {
          .data = (const char *)entry,
          .len = IPV6_LEN,
      },
      .port = ((u8)entry[IPV6_LEN] << 8) | (u8)entry[IPV6_LEN + 1],
  };
}

void bencodeFileDecode(BencodeParser *parser, TorrentInfoFiles *files) {
  assert(bencodeParserCurrent(parser) == 'd');

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
      assert(bencodeParserCurrent(parser) == 'l');
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
}

void bencodeInfoDictDecode(BencodeParser *parser, TorrentMetainfo *metainfo) {
  assert(bencodeParserCurrent(parser) == 'd');

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

      assert(bencodeParserCurrent(parser) == 'l');
      parser->cursor++;
      while (parser->bencode[parser->cursor] != 'e') {
        bencodeFileDecode(parser, &metainfo->info.multi_files);
      }
      parser->cursor++;
      continue;
    }

    printf("-> info|%.*s\n", (u32)key.len, key.data);
    bencodeValueSkip(parser);
    continue;
  }
  parser->cursor++;
  assert(parser->bencode[start] == 'd');
  assert(parser->bencode[parser->cursor - 1] == 'e');
}

void torrentMetainfoDecode(BencodeParser *parser, TorrentMetainfo *metainfo) {
  assert(bencodeParserCurrent(parser) == 'd');
  parser->cursor++;
  while (parser->bencode[parser->cursor] != 'e') {
    String key = bencodeStringDecode(parser);

    if (STRING_MATCHES("failure reason", key)) {
      String str = bencodeStringDecode(parser);
      printf("failure reason: %.*s\n", (u32)str.len, str.data);
      continue;
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
      assert(bencodeParserCurrent(parser) == 'l');
      parser->cursor++;
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
        metainfo->trackers_url[metainfo->trackers_count] = bencodeStringDecode(parser);
        metainfo->trackers_count++;
      }
      parser->cursor++;
      continue;
    }

    printf("-> |%.*s\n", (u32)key.len, key.data);
    bencodeValueSkip(parser);
    continue;
  }
  parser->cursor++;
}

char *bencodeDictKeyEncode(char *key, char *dest) {
  char tmp[128] = {0};
  usize len = strlen(key);
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

void bencodeInfoDictEncode(TorrentMetainfo *metainfo) {
  char buff[1024 * 1024] = {0};
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

  printf("SHA1: %s\n", metainfo->info_hash);
}

u32 torrentResponseDecode(String *raw_resp, TrackerResponse *resp) {
  BencodeParser p = bencodeParserFromData((char *)raw_resp->data, raw_resp->len);

  if (p.bencode[p.cursor] != 'd') {
    resp->failure_reason = (String){.data = p.bencode, .len = p.bencode_len};
    return 0;
  }

  p.cursor++;
  while (p.bencode[p.cursor] != 'e') {
    String key = bencodeStringDecode(&p);

    if (STRING_MATCHES("failure reason", key)) {
      resp->failure_reason = bencodeStringDecode(&p);
      return 0;
    }

    else if (STRING_MATCHES("complete", key)) {
      resp->complete = bencodeIntegerDecode(&p);
      continue;
    }

    else if (STRING_MATCHES("downloaded", key)) {
      resp->downloaded = bencodeIntegerDecode(&p);
      continue;
    }

    else if (STRING_MATCHES("incomplete", key)) {
      resp->incomplete = bencodeIntegerDecode(&p);
      continue;
    }

    else if (STRING_MATCHES("interval", key)) {
      resp->interval = bencodeIntegerDecode(&p);
      continue;
    }

    else if (STRING_MATCHES("min interval", key)) {
      resp->min_interval = bencodeIntegerDecode(&p);
      continue;
    }

    else if (STRING_MATCHES("peers", key)) {
      if (p.bencode[p.cursor] == 'l') {
        bencodeValueSkip(&p);
        printf("----- decoding peers: only compact form decoding is implemented yet.\n");
        continue;
      }

      String str = bencodeStringDecode(&p);
      if (str.len % (IPV4_LEN + PORT_LEN) != 0) {
        printf("----- decoding peers4: invalid length (must be multiple of 10).\n");
        continue;
      }

      resp->peers = (TorrentPeers){
          .data = str.data,
          .len = str.len,
          .count = str.len / (IPV4_LEN + PORT_LEN),
      };
      continue;
    }

    else if (STRING_MATCHES("peers6", key)) {
      if (p.bencode[p.cursor] == 'l') {
        bencodeValueSkip(&p);
        printf("----- decoding peers6: only compact form decoding is implemented yet.\n");
        continue;
      }

      // printf("----- decoding peers6: nintendo switch does not support ipv6.\n");
      // bencodeValueSkip(p);
      // continue;

      String str = bencodeStringDecode(&p);
      if (str.len % (IPV6_LEN + PORT_LEN) != 0) {
        printf("----- decoding peers6: invalid length (must be multiple of 18).\n");
        continue;
      }

      resp->peers6 = (TorrentPeers6){
          .data = str.data,
          .len = str.len,
          .count = str.len / (IPV6_LEN + PORT_LEN),
      };
      continue;
    }

    else if (STRING_MATCHES("warning message", key)) {
      resp->warning_message = bencodeStringDecode(&p);
      continue;
    }

    // skip unrecognized/unwanted key/values
    printf("-> |%.*s\n", (u32)key.len, key.data);
    bencodeValueSkip(&p);
    continue;
  }
  p.cursor++;
  return 0;
}
