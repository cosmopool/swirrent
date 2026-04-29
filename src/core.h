#pragma once

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define NANOSECONDS_IN_MILLI 1000000

#define IPV6_LEN 16
#define IPV4_LEN 4
#define PORT_LEN 2

#define PORT "6666"
#define MAX_TRIES 2

#define SHA_DIGEST_LENGTH 20
#define PEER_ID_LENGTH 20

#define MAX_PEERS 128

#define UNREACHABLE(msg)                                       \
  fprintf(stderr,                                              \
          "%s:%u: execution reached a UNREACHABLE line: %s\n", \
          __FILE__, __LINE__, (msg));                          \
  exit(52);
#define ASSERT(expr, msg)                                         \
  if (!(expr)) {                                                  \
    fprintf(stderr,                                               \
            "%s:%u: failed assertion (" __STRING(expr) "): %s\n", \
            __FILE__, __LINE__, (msg));                           \
    exit(1);                                                      \
  }

// core types
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef intptr_t isize;

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef uintptr_t usize;

typedef float f32;
typedef double f64;
// core types

// ---------- DynamicArray

/*
 * You must implement a struct like this to use the DA_* macros
struct {
  u32 capacity;
  u32 count;
  Type *data;
};
*/

#define DA_INIT(Name, initial_capacity)                          \
  (Name){                                                        \
      .capacity = initial_capacity,                              \
      .data = calloc(initial_capacity, sizeof(*(Name){0}.data)), \
  };

#define DA_DEINIT(da) free((da).data);

#define ARRAY_TYPE(a) typeof(*(a)->data)

#define DA_INSERT(da, value)                                                         \
  if ((da)->count + 1 > (da)->capacity) {                                            \
    (da)->capacity *= 2;                                                             \
    (da)->data = realloc((da)->data, (da)->capacity);                                \
  }                                                                                  \
  ASSERT((da)->count < (da)->capacity, "count should always be less then capacity"); \
  ((ARRAY_TYPE((da)) *)(da)->data)[(da)->count] = value;                             \
  (da)->count++;

// ---------- String
/**
 * A string structure that combines length information with character data.
 * This structure provides a safer way to handle strings by storing both
 * the string content and its length together, avoiding reliance on
 * null-terminated strings.
 *
 * The structure contains:
 * - `len`: The length of the string in bytes
 * - `str`: Pointer to the character array (may not be null-terminated)
 *
 * @see CL_stringNew()
 * @see CL_stringNewC()
 */
typedef struct String {
  usize len;
  const char *data;
} String;

#include <assert.h>
#include <string.h>

/**
 * Creates a new String with specified length and content.
 * A more elaborate description would go here explaining the string structure.
 * @param len Length of the string (must be greater than 0)
 * @param str Pointer to the character array
 * @see mcl_stringNewC()
 * @return A new String struct containing the provided string data
 */
inline String mclStringNew(usize len, const char *str) {
  assert(len > 0);
  String s = {len, str};
  return (s);
}

/**
 * Creates a new String from a C-style null-terminated string.
 * Automatically calculates the string length using strlen().
 * It does not make a copy of the provided string, it just reference it.
 * @param str Null-terminated C string to convert
 * @see mcl_stringNew()
 * @return A new String struct containing the provided string data
 */
inline String mclStringNewC(const char *str) {
  usize len = strlen(str);
  String s = {len, str};
  return s;
}

static inline void mclPrintString(String str) {
  printf("%.*s", (u32)str.len, str.data);
}

// ---------- String

// ---------- Slices

typedef struct {
  void *start;
  u32 len;
} Slice;

// ---------- Slices

inline void mclExitMsg(u32 exit_code, const char *fmt, ...) {
  va_list args;

  va_start(args, fmt);
  fprintf(stderr, fmt, args);
  va_end(args);

  exit(exit_code);
}

// ---------- Utils

static inline void hexdump(const char *format, u8 *data, u64 len, u8 breakline) {
  for (u64 i = 0; i < len; i++) {
    printf(format, data[i]);
    if (!breakline) continue;
    if ((i + 1) % 16 == 0) printf("\n");
  }
  printf("\n");
}

static inline void generatePeerId(u8 *buf) {
  const char *prefix = "SW-0001-";
  usize size = strlen(prefix);
  memcpy(buf, prefix, size);
  for (int i = size; i < 20; i++) buf[i] = rand() & 0xff;
}
