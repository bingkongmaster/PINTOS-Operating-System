#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include <debug.h>

struct cache_table cache_table;

void cache_table_init(void) {

  // initialize cache table members
  list_init(&cache_table.list);
  lock_init(&cache_table.lock);
  cache_table.size = 0;
  cache_table.destroyed = 0;

  // start thread flushing buffer cache
  thread_create("flush", PRI_DEFAULT, cache_table_thread, NULL);
}

struct cache_table_entry *cache_table_find(disk_sector_t block) {

  struct list_elem *e;
  for (e=list_begin(&cache_table.list); e!=list_end(&cache_table.list); e=list_next(e)) {
    struct cache_table_entry *cte = list_entry(e, struct cache_table_entry, elem);
    if (cte->block == block) {
      return cte;
    }
  }
  return NULL;
}

struct cache_table_entry *allocate_cache(disk_sector_t block) {

  // should be guaranteed that the entry with block does not exist
  struct cache_table_entry *cte = (struct cache_table_entry *)malloc(sizeof(struct cache_table_entry));
  cte->block = block;
  cte->vaddr = (uint8_t *)malloc(DISK_SECTOR_SIZE);

  ASSERT(cache_table.size <= CACHE_TABLE_MAX_SIZE);

  if (cache_table.size == CACHE_TABLE_MAX_SIZE) {
    // cache table size reached the limit
    struct cache_table_entry *victim = list_entry(list_pop_front(&cache_table.list), struct cache_table_entry, elem);

    // WARNING: consider dirty bit
    disk_write(filesys_disk, victim->block, victim->vaddr);
    free(victim->vaddr);
    free(victim);
    cache_table.size--;
  }

  list_push_back(&cache_table.list, &cte->elem);
  cache_table.size++;

  return cte;
}

void free_cache(disk_sector_t block) {
  lock_acquire(&cache_table.lock);
  struct list_elem *e;
  for (e=list_begin(&cache_table.list); e!=list_end(&cache_table.list); e=list_next(e)) {
    struct cache_table_entry *cte = list_entry(e, struct cache_table_entry, elem);
    if (cte->block == block) {
      list_remove(&cte->elem);
      free(cte->vaddr);
      free(cte);
      break;
    }
  }
  lock_release(&cache_table.lock);
}

void cache_table_read(uint8_t *buffer, disk_sector_t sector, int offset, int size) {

  lock_acquire(&cache_table.lock);
  struct cache_table_entry *cte = cache_table_find(sector);
  if (cte) {
    // read from buffer cache
    memcpy(buffer, cte->vaddr + offset, size);
  } else {
    // read from disk into buffer cache and copy it to destination buffer
    cte = allocate_cache(sector);
    disk_read(filesys_disk, sector, cte->vaddr);
    memcpy(buffer, cte->vaddr + offset, size);
  }
  lock_release(&cache_table.lock);
}

void cache_table_write(uint8_t *buffer, disk_sector_t sector, int offset, int size) {

  lock_acquire(&cache_table.lock);
  struct cache_table_entry *cte = cache_table_find(sector);
  if (cte) {
    // write to buffer cache
    memcpy(cte->vaddr + offset, buffer, size);
  } else {
    // read from disk into buffer cache and copy it to buffer
    cte = allocate_cache(sector);
    disk_read(filesys_disk, sector, cte->vaddr);
    memcpy(cte->vaddr + offset, buffer, size);
  }
  lock_release(&cache_table.lock);
}

void cache_table_flush(void) {
    lock_acquire(&cache_table.lock);
    struct list_elem *e;
    for (e=list_begin(&cache_table.list); e!=list_end(&cache_table.list); e=list_next(e)) {
      struct cache_table_entry *cte = list_entry(e, struct cache_table_entry, elem);
      disk_write(filesys_disk, cte->block, cte->vaddr);
    }
    lock_release(&cache_table.lock);
}

void cache_table_thread(void *aux UNUSED) {
  while (!cache_table.destroyed) {
    cache_table_flush();
    timer_msleep(CACHE_TABLE_FLUSH_PERIOD);
  }
}

void cache_table_destroy(void) {
  cache_table_flush();

  while (!list_empty(&cache_table.list)) {
    struct cache_table_entry *cte = list_entry(list_pop_front(&cache_table.list), struct cache_table_entry, elem);
    free(cte->vaddr);
    free(cte);
  }

  cache_table.destroyed = true;
}
