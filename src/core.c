#include "core.h"
#include <assert.h>
#include <string.h>

String stringNew(usize len, const u8 *str) {
  assert(len > 0);
  String s = {len, str};
  return (s);
}

String stringNewC(const char *str) {
  usize len = strlen(str);
  String s = {len, (const u8 *)str};
  return s;
}
