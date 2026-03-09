#include "bencode.h"
#define STRING_IMPLEMENTATION
#include "core.h"
#include "teeny-sha1.c"
#define TORRENT_IMPLEMENTATION
#include "torrent.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ANNOUNCE 7572165266058428
#define NAME 6385503302
#define PIECE_LENGTH -2824521067031402771
#define PIECES 6953900567806
#define INFO 6385337553
#define LENGTH 6953739610823
#define SHA_DIGEST_LENGTH 20

// static Arena default_arena = {0};
static TorrentMetainfo metainfo = {0};

// http://www.cse.yorku.ca/~oz/hash.html
isize hash(char *str) {
  isize hash = 5381;
  i32 c;

  while ((c = *str++)) {
    hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
  }

  return hash;
}

// http://www.cse.yorku.ca/~oz/hash.html
isize hashString(String str) {
  char a[1024] = {0};
  for (u32 i = 0; i < str.len; i++) {
    a[i] = str.data[i];
  }
  a[str.len] = '\0';
  isize h = hash(a);
  // printf("HASH: %ld | STRING: %s\n", h, a);
  return h;
}

void openDict(BencodeParser *parser, char *key) {
  usize key_hash = hash(key);
  parser->dict_stack_pos++;
  parser->dict_stack[parser->dict_stack_pos] = key_hash;
}

void closeDict(BencodeParser *parser) {
  parser->dict_stack[parser->dict_stack_pos] = 0;
  parser->dict_stack_pos--;
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

BencodeValue bencode_decode(BencodeParser *parser, String bencode) {
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
      parser->cursor++;
      usize dict_start = parser->cursor;
      while (bencode.data[parser->cursor] != 'e') {
        String key = parseString(parser, bencode);
        usize key_hash = hashString(key);
        if (memcmp(key.data, "announce", 8) == 0) {
          metainfo.announce = parseString(parser, bencode);
          continue;
        }

        if (memcmp(key.data, "name", 4) == 0) {
          metainfo.name = parseString(parser, bencode);
          continue;
        }

        if (memcmp(key.data, "piece length", 12) == 0) {
          metainfo.piece_length = parseInteger(parser, bencode);
          continue;
        }

        if (memcmp(key.data, "pieces", 6) == 0) {
          metainfo.is_single_file = true;
          metainfo.pieces = parseString(parser, bencode);
          continue;
        }

        if (memcmp(key.data, "length", 6) == 0) {
          metainfo.is_single_file = true;
          metainfo.single_file.length = parseInteger(parser, bencode);
          continue;
        }

        if (memcmp(key.data, "info", 4) == 0) {
          u8 hash[SHA_DIGEST_LENGTH];
          usize start = parser->cursor;
          bencode_decode(parser, bencode);
          usize end = parser->cursor - 1;
          assert(bencode.data[start] == 'd');
          assert(bencode.data[end] == 'e');
          if (sha1digest(hash, NULL, (u8 *)&bencode.data[start], end - start) !=
              0)
            exit(1);

          char sha1string[SHA_DIGEST_LENGTH * 2 + 1];
          for (int i = 0; i < SHA_DIGEST_LENGTH; ++i) {
            sprintf(&sha1string[i * 2], "%02x", (u32)hash[i]);
          }
          printf("Info hash: %s \n", sha1string);
          continue;
        }

        BencodeValue value = bencode_decode(parser, bencode);
        (void)value;
        continue;
      }
      parser->cursor++;
      usize dict_end = parser->cursor;
      BencodeValue value = {0};
      value.kind = DICT;
      value.string = (String){.len = dict_end - dict_start,
                              .data = &bencode.data[dict_start]};
      return value;
    }

    case 'l': {
      parser->cursor++;
      usize start = parser->cursor;
      // printf("LIST: [");
      while (bencode.data[parser->cursor] != 'e') {
        BencodeValue v = bencode_decode(parser, bencode);
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
      parser->cursor++;
      usize end = parser->cursor;
      BencodeValue value = {0};
      value.kind = LIST;
      value.string = (String){.len = end - start, .data = &bencode.data[start]};
      return value;
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

i32 main(i32 argc, char **argv) {
  FILE *file_ptr = fopen("f.torrent", "rb");
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
  printf("FILE SIZE: %luK\n", file_length / 1024);

  String bencode = {
      .len = file_length,
      .data = file_content,
  };

  // printf("HASH: %ld\n", hash("info"));

  BencodeParser parser = {0};
  assert(bencode.data[parser.cursor] == 'd');
  bencode_decode(&parser, bencode);

  printf("meta info\n");
  printf("announce: ");
  mcl_printString(metainfo.announce);
  printf("\n");
  printf("name: ");
  mcl_printString(metainfo.name);
  printf("\n");
  if (metainfo.is_single_file) {
    printf("piece_length: %ldK\n", metainfo.piece_length / 1024);
    printf("length: %ldM\n", metainfo.single_file.length / 1024 / 1024);
    printf("pieces: %lu", metainfo.pieces.len / 20);
  } else {
  }
  printf("\n");

  return 0;
}
