#include <string.h>
#define ARENA_IMPLEMENTATION
#include "../include/unity.h"
#include "../src/bencode.h"
#include "../src/linked_list.h"

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
  const char *encoded = "i-11e";
  usize len = strlen(encoded);
  BencodeReturn_t result = bencode_decode(&default_arena, encoded, len);
  TEST_ASSERT_EQUAL_INT64(-11, *(usize *)result.data);
}

void test_integer_parsing_should_error_when_missing_enclosing_e(void) {
  const char *encoded = "i-11";
  usize len = strlen(encoded);
  BencodeReturn_t result = bencode_decode(&default_arena, encoded, len);
  TEST_ASSERT_EQUAL_INT64(BENCODE_KIND_NONE, result.kind);
}

void test_integer_parsing_should_error_when_str_len_less_then_3(void) {
  const char *encoded = "ie";
  usize len = strlen(encoded);
  BencodeReturn_t result = bencode_decode(&default_arena, encoded, len);
  TEST_ASSERT_EQUAL_INT64(BENCODE_KIND_NONE, result.kind);
}

void test_integer_parsing_should_error_on_invalid_str(void) {
  const char *encoded = "i-e";
  usize len = strlen(encoded);
  BencodeReturn_t result = bencode_decode(&default_arena, encoded, len);
  TEST_ASSERT_EQUAL_INT64(BENCODE_KIND_NONE, result.kind);
}

// ----------------------------------------------------------------------
// STRINGS TESTS
// ----------------------------------------------------------------------

void test_string_parsing_should_succeed(void) {
  const char *encoded = "5:hello";
  usize len = strlen(encoded);
  BencodeReturn_t result = bencode_decode(&default_arena, encoded, len);
  TEST_ASSERT_EQUAL_INT64(BENCODE_KIND_STRING, result.kind);
  TEST_ASSERT_EQUAL_STRING("hello", result.data);
}

void test_big_string_parsing_should_succeed(void) {
  const char *encoded =
      "72:AooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooA";
  usize len = strlen(encoded);
  BencodeReturn_t result = bencode_decode(&default_arena, encoded, len);
  TEST_ASSERT_EQUAL_INT64(BENCODE_KIND_STRING, result.kind);
  TEST_ASSERT_EQUAL_STRING(&encoded[3], result.data);
}

// ----------------------------------------------------------------------
// LISTS TESTS
// ----------------------------------------------------------------------

void test_list_parsing_should_succeed(void) {
  const char *encoded = "l5:helloe";
  usize len = strlen(encoded);
  BencodeReturn_t result = bencode_decode(&default_arena, encoded, len);

  ListNode_t *head = (ListNode_t *)result.data;
  TEST_ASSERT_TRUE(head);
  TEST_ASSERT_EQUAL_INT64(BENCODE_KIND_LIST, result.kind);

  // first
  BencodeValue_t *node = head->next->value;
  TEST_ASSERT_TRUE(node);
  TEST_ASSERT_EQUAL_INT64(BENCODE_KIND_STRING, node->kind);
  TEST_ASSERT_EQUAL_STRING("hello", node->data);
}

void test_list_parsing_multiple_items_should_succeed(void) {
  const char *encoded = "l5:helloi10ee";
  usize len = strlen(encoded);
  BencodeReturn_t result = bencode_decode(&default_arena, encoded, len);

  ListNode_t *head = (ListNode_t *)result.data;
  TEST_ASSERT_TRUE(head);
  TEST_ASSERT_EQUAL_INT64(BENCODE_KIND_LIST, result.kind);

  // first
  ListNode_t *node = head->next;
  TEST_ASSERT_TRUE(node->value);
  BencodeValue_t *value = node->value;
  TEST_ASSERT_EQUAL_INT64(BENCODE_KIND_STRING, value->kind);
  TEST_ASSERT_EQUAL_STRING("hello", value->data);

  // second
  node = head->next->next;
  TEST_ASSERT_TRUE(node->value);
  value = node->value;
  TEST_ASSERT_EQUAL_INT64(10, *(usize *)value->data);
  TEST_ASSERT_FALSE(node->next);
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
  RUN_TEST(test_list_parsing_should_succeed);
  RUN_TEST(test_list_parsing_multiple_items_should_succeed);
  return UNITY_END();
}
