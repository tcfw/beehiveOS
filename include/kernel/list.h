#ifndef _KERNEL_LIST_H
#define _KERNEL_LIST_H

#include <kernel/stdbool.h>

struct list_head
{
	struct list_head *next, *prev;
};

#define LIST_HEAD_INIT(name) \
	{                        \
		&(name), &(name)     \
	}

#define LIST_HEAD(name) \
	struct list_head name = LIST_HEAD_INIT(name)

#define list_empty(list) \
	(list.next == &list)

static inline void INIT_LIST_HEAD(struct list_head *list)
{
	list->next = list;
	list->prev = list;
}

static inline void list_del(struct list_head *entry)
{
	struct list_head *prev = entry->prev;
	struct list_head *next = entry->next;
	prev->next = next;
	next->prev = prev;
}

static inline bool list_is_empty(struct list_head *head)
{
	return head->next == head;
}

int list_is_head(struct list_head *pos, struct list_head *head);
struct list_head *_list_prepend(struct list_head *list, struct list_head *entry);
struct list_head *_list_append(struct list_head *list, struct list_head *entry);

static inline void __list_add(struct list_head *new,
							  struct list_head *prev,
							  struct list_head *next)
{
	next->prev = new;
	new->next = next;
	new->prev = prev;
	prev->next = new;
}

static inline void list_add(struct list_head *new, struct list_head *head)
{
	__list_add(new, head, head->next);
}

static inline void list_add_tail(struct list_head *new, struct list_head *head)
{
	__list_add(new, head->prev, head);
}

static inline void list_add_after(struct list_head *new, struct list_head *existing)
{
	__list_add(new, existing, existing->next);
}

static inline void list_add_before(struct list_head *new, struct list_head *existing)
{
	__list_add(new, existing->prev, existing);
}

#define list_head_for_each(pos, head) \
	for (pos = (head)->next; !list_is_head((struct list_head *)pos, (struct list_head *)head); pos = pos->list.next)

#define list_head_for_each_safe(pos, n, head) \
	for (pos = (head)->next, n = pos->list.next; !list_is_head((struct list_head *)pos, (struct list_head *)head); pos = n, n = pos->list.next)

#define list_for_each(pos, head) \
	for (pos = (head)->next; !list_is_head((struct list_head *)pos, (struct list_head *)head); pos = pos->next)

#define list_for_each_safe(pos, n, head) \
	for (pos = (head)->next, n = pos->next; !list_is_head((struct list_head *)pos, (struct list_head *)head); pos = n, n = pos->next)

static inline unsigned long long list_len(struct list_head *head)
{
	unsigned long long count = 0;

	struct list_head *pos;
	list_for_each(pos, head)
		count++;

	return count;
}

#endif