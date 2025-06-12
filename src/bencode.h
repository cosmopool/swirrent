#include "../include/arena.h"
#include "core.h"

#include <stdbool.h>

bool is_int(char c);

bool is_string(char c);

bool is_list(char c);

bool is_dict(char c);

bool is_end(char c);

i32 bencode_decode(Arena *arena, const char *bencode, usize len);
