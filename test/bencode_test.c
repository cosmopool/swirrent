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
  i32 result = bencode_decode(&default_arena, "i-11e", 5);
  TEST_ASSERT_EQUAL_INT32(0, result);
}

void test_integer_parsing_should_error_when_missing_enclosing_e(void) {
  i32 result = bencode_decode(&default_arena, "i-11", 4);
  TEST_ASSERT_EQUAL_INT32(-1, result);
}

void test_integer_parsing_should_error_when_str_len_less_then_3(void) {
  i32 result = bencode_decode(&default_arena, "ie", 3);
  TEST_ASSERT_EQUAL_INT32(-1, result);
}

void test_integer_parsing_should_error_on_invalid_str(void) {
  i32 result = bencode_decode(&default_arena, "i-e", 3);
  TEST_ASSERT_EQUAL_INT32(-1, result);
}

// not needed when using generate_test_runner.rb
int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_integer_parsing_should_succeed);
  RUN_TEST(test_integer_parsing_should_error_when_missing_enclosing_e);
  RUN_TEST(test_integer_parsing_should_error_when_str_len_less_then_3);
  RUN_TEST(test_integer_parsing_should_error_on_invalid_str);
  return UNITY_END();
}
