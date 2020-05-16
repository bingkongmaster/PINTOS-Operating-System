#ifndef CACHE_H
#define CACHE_H

#include "devices/disk.h"
#include "devices/timer.h"
#include "threads/synch.h"
#include <list.h>
#include <string.h>
#include <debug.h>

#define CACHE_TABLE_MAX_SIZE 64
#define CACHE_TABLE_FLUSH_PERIOD 50 // millisecond

struct cache_table_entry {
  disk_sector_t block;
  uint8_t *vaddr; // kernel virtual memory, shared address among all threads
  struct list_elem elem;
};

struct cache_table {
  struct list list;
  int size;
  struct lock lock;
  bool destroyed; // true: filesys_done called
};

void cache_table_init(void);
struct cache_table_entry *cache_table_find(disk_sector_t sector);
struct cache_table_entry *allocate_cache(disk_sector_t sector);
void free_cache(disk_sector_t sector);
void cache_table_read(uint8_t *buffer, disk_sector_t sector, int offset, int size);
void cache_table_write(uint8_t *buffer, disk_sector_t sector, int offset, int size);
void cache_table_flush(void);
void cache_table_thread(void *aux UNUSED);
void cache_table_destroy(void);

#endif
