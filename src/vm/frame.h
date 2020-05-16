#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <debug.h>
#include <hash.h>
#include "userprog/process.h"
#include "threads/palloc.h"

struct frame_table_entry
{
	uint8_t *frame;
	struct process* owner;
	uint8_t *page;

	struct hash_elem hash_elem;
	struct list_elem list_elem;
};

struct frame_table {
	struct hash hash;
	struct list list;
	struct lock lock;
};

void frame_table_init (void);

uint8_t *frame_table_select_victim();
void frame_table_insert(struct process *process, uint8_t *frame, uint8_t *page);
void frame_table_remove(uint8_t *frame);
struct frame_table_entry *frame_table_find(uint8_t *frame);

uint8_t *allocate_frame (uint8_t *page, bool writable, enum palloc_flags flag);
#endif /* vm/frame.h */
