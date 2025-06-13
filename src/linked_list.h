#pragma once
#include "bencode.h"
#include "core.h"

#define NODE ListNode *

typedef struct ListNode {
  struct ListNode *head;
  struct ListNode *next;
  struct ListNode *tail;
  BencodeValue_t *value;
} ListNode_t;

ListNode_t *listInit(Arena *a);

ListNode_t *listAppend(Arena *a, ListNode_t *list, void *data);
ListNode_t *listAppendAlloc(Arena *a, ListNode_t *list, void *data, usize size);
