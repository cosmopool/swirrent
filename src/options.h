#pragma once

#include <stdbool.h>

typedef struct {
  bool verbose;
  bool decode_only;
  char *torrent_path;
} Options;
