#define ARENA_IMPLEMENTATION
#include "../include/unity.h"
#include "../src/bencode.h"
#include <stdio.h>

static Arena default_arena = {0};

void setUp(void) {
}

void tearDown(void) {
  // arena_reset(&default_arena);
}

// ----------------------------------------------------------------------
// INTEGER TESTS
// ----------------------------------------------------------------------
void test_integer_parsing_should_succeed(void) {
  BencodeReturn_t result = bencode_decode(&default_arena, "i-11e", 5);
  TEST_ASSERT_EQUAL_INT64(-11, *(usize *)result.data);
}

void test_integer_parsing_should_error_when_missing_enclosing_e(void) {
  BencodeReturn_t result = bencode_decode(&default_arena, "i-11", 4);
  TEST_ASSERT_EQUAL_INT64(BENCODE_KIND_NONE, result.kind);
}

void test_integer_parsing_should_error_when_str_len_less_then_3(void) {
  BencodeReturn_t result = bencode_decode(&default_arena, "ie", 2);
  TEST_ASSERT_EQUAL_INT64(BENCODE_KIND_NONE, result.kind);
}

void test_integer_parsing_should_error_on_invalid_str(void) {
  BencodeReturn_t result = bencode_decode(&default_arena, "i-e", 3);
  TEST_ASSERT_EQUAL_INT64(BENCODE_KIND_NONE, result.kind);
}

// ----------------------------------------------------------------------
// STRINGS TESTS
// ----------------------------------------------------------------------

void test_string_parsing_should_succeed(void) {
  BencodeReturn_t result = bencode_decode(&default_arena, "5:hello", 7);
  TEST_ASSERT_EQUAL_INT64(BENCODE_KIND_STRING, result.kind);
  TEST_ASSERT_EQUAL_STRING("hello", result.data);
}

void test_big_string_parsing_should_succeed(void) {
  BencodeReturn_t result = bencode_decode(
      &default_arena,
      "72:AooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooA",
      7);
  TEST_ASSERT_EQUAL_INT64(BENCODE_KIND_STRING, result.kind);
  TEST_ASSERT_EQUAL_STRING(
      "AooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooA",
      result.data);
}

// not needed when using generate_test_runner.rb
int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_integer_parsing_should_succeed);
  RUN_TEST(test_integer_parsing_should_error_when_missing_enclosing_e);
  RUN_TEST(test_integer_parsing_should_error_when_str_len_less_then_3);
  RUN_TEST(test_integer_parsing_should_error_on_invalid_str);
  RUN_TEST(test_string_parsing_should_succeed);
  RUN_TEST(test_big_string_parsing_should_succeed);
  return UNITY_END();
}
