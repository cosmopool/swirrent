#pragma once
#include <stdint.h>

typedef int8_t i8;
typedef uint8_t u8;
typedef int16_t i16;
typedef uint16_t u16;
typedef int32_t i32;
typedef uint32_t u32;
typedef int64_t i64;
typedef uint64_t u64;
/// Size of a signed pointer
typedef intptr_t isize;
/// Size of a unsigned pointer
typedef uintptr_t usize;

typedef float f32;
typedef double f64;

typedef struct {
  usize len;
  const u8 *str;
} String;

String stringNew(usize len, const u8 *str);
String stringNewC(const char *str);
