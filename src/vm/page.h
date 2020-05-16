#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <debug.h>
#include <hash.h>
#include "devices/disk.h"
#include "filesys/file.h"
#include "filesys/inode.h"

struct page_table_entry {
	uint8_t *page;
	uint8_t *frame;
	disk_sector_t block;
	struct file *file;
	off_t offset;

	struct hash_elem hash_elem;

	bool disk;  // indicates whether if the page is in disk
};

struct page_table {
	struct process *owner;
	struct hash hash;
};

/*
struct sup_page_table_entry
{
	uint8_t* user_vaddr;
	uint64_t access_time;

	bool dirty;
	bool accessed;
};
*/

void page_table_init(struct page_table *page_table);

void page_table_insert_frame(struct page_table *page_table, uint8_t *page, uint8_t *frame);
void page_table_insert_block(struct page_table *page_table, uint8_t *page, disk_sector_t block);
void page_table_insert_file(struct page_table *page_table, uint8_t *page, struct file *file, off_t offset);
struct page_table_entry *page_table_find(struct page_table *page_table, uint8_t *page);
void page_table_remove(struct page_table *page_table, uint8_t *page);
void page_table_free(struct page_table *page_table);

//struct sup_page_table_entry *allocate_page (void *addr);

#endif /* vm/page.h */
