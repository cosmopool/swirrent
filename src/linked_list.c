#include "linked_list.h"
#include <string.h>

ListNode_t *listInit(Arena *a) {
  ListNode_t *list = (ListNode_t *)arena_alloc(a, sizeof(ListNode_t));
  list->head = NULL;
  list->next = NULL;
  list->tail = NULL;
  return list;
}

ListNode_t *listAppend(Arena *a, ListNode_t *list, void *data) {
  ListNode_t *node = arena_alloc(a, sizeof(ListNode_t));
  node->value = data;

  if (!list->next) {
    list->head = list;
    list->next = node;
    list->tail = node;
  } else {
    list->tail->next = node;
  }

  return node;
}

ListNode_t *listAppendAlloc(Arena *a, ListNode_t *list, void *data,
                            usize size) {
  void *data_alloc = arena_alloc(a, size);
  memcpy(data_alloc, data, size);
  return listAppend(a, list, data_alloc);
}
