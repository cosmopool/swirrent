#pragma once
#include <stdint.h>

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
typedef struct {
  usize len;
  const char *str;
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
inline String mcl_stringNew(usize len, const char *str) {
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
inline String mcl_stringNewC(const char *str) {
  usize len = strlen(str);
  String s = {len, str};
  return s;
}

// ---------- String

// ---------- Slices

typedef struct {
  void *start;
  u32 len;
} Slice;

// ---------- Slices
