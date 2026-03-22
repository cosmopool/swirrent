#pragma once

#include <stdbool.h>

typedef struct {
  bool verbose;
  bool decode_only;
  bool dump_response;
  char *torrent_path;
  char *raw_request_path;
  char *raw_request_output_path;
} Options;
