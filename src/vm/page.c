#include <bitmap.h>
#include "threads/malloc.h"
#include "userprog/process.h"
#include "vm/page.h"
#include "vm/frame.h"
#include "vm/swap.h"

extern int block_size;
extern struct bitmap *swap_table;
extern struct lock swap_lock;
extern struct frame_table frame_table;
extern struct lock lock_file;

unsigned page_hash (const struct hash_elem *e, void *aux);
bool page_less (const struct hash_elem *a, const struct hash_elem *b, void *aux);

unsigned page_hash (const struct hash_elem *e, void *aux) {
    struct page_table_entry *pte = hash_entry(e, struct page_table_entry, hash_elem);
    return hash_bytes(&pte->page, sizeof(pte->page));
}

bool page_less (const struct hash_elem *a, const struct hash_elem *b, void *aux) {
    struct page_table_entry *pte1 = hash_entry(a, struct page_table_entry, hash_elem);
    struct page_table_entry *pte2 = hash_entry(b, struct page_table_entry, hash_elem);
    return pte1->page < pte2->page;
}

/*
 * Initialize supplementary page table
 */
void page_table_init(struct page_table *page_table)
{
    #ifdef DEBUG
    printf("[page_table_init]\n");
    #endif

    hash_init(&page_table->hash, page_hash, page_less, NULL);
}

void page_table_insert_file(struct page_table *page_table, uint8_t *page, struct file *file, off_t offset) {
  #ifdef DEBUG
  printf("[page_table_insert_file] page: %x, offset: %u\n", page, offset);
  #endif

  struct page_table_entry *pte = page_table_find(page_table, page);
  if (pte == NULL) {
      pte = (struct page_table_entry *)malloc(sizeof(struct page_table_entry));
      pte->frame = NULL;
      pte->disk = false;
      pte->file = NULL;
  }
  pte->page = page;
  pte->frame = NULL;
  pte->block = 0;
  pte->disk = false;
  pte->file = file;
  pte->offset = offset;

  // insert to page table, consider replacement
  hash_replace(&page_table->hash, &pte->hash_elem);
}

void page_table_insert_block(struct page_table *page_table, uint8_t *page, disk_sector_t block) {
    #ifdef DEBUG
    printf("[page_table_insert_block] page: %x, block: %u\n", page, block);
    #endif

    struct page_table_entry *pte = page_table_find(page_table, page);
    if (pte == NULL) {
        pte = (struct page_table_entry *)malloc(sizeof(struct page_table_entry));
        pte->frame = NULL;
        pte->disk = false;
        pte->file = NULL;
    }
    pte->page = page;
    pte->frame = NULL;
    pte->block = block;
    pte->disk = true;

    // insert to page table, consider replacement
    hash_replace(&page_table->hash, &pte->hash_elem);
}

void page_table_insert_frame(struct page_table *page_table, uint8_t *page, uint8_t *frame) {
    #ifdef DEBUG
    printf("[page_table_insert_frame] page: %x, frame: %x\n", page, frame);
    #endif

    struct page_table_entry *pte = page_table_find(page_table, page);
    if (pte == NULL) {
        pte = (struct page_table_entry *)malloc(sizeof(struct page_table_entry));
        pte->frame = NULL;
        pte->disk = false;
        pte->file = NULL;
    }
    pte->page = page;
    pte->frame = frame;
    pte->block = 0;
    pte->disk = false;

    // insert to page table, consider replacement
    hash_replace(&page_table->hash, &pte->hash_elem);
}

struct page_table_entry *page_table_find(struct page_table *page_table, uint8_t *page) {
    #ifdef DEBUG
    printf("[page_table_find] page: %x\n", page);
    #endif

    struct page_table_entry pte;
    pte.page = page;
    struct hash_elem *e = hash_find(&page_table->hash, &pte.hash_elem);
    if(e == NULL) {
        return NULL;
    }
    return hash_entry(e, struct page_table_entry, hash_elem);
}

void page_table_remove(struct page_table *page_table, uint8_t *page) {
  #ifdef DEBUG
  printf("[page_table_remove] page: %x\n", page);
  #endif

  struct page_table_entry *pte = page_table_find(page_table, page);
  pagedir_clear_page(page_table->owner->thread->pagedir, page);
  hash_delete(&page_table->hash, &pte->hash_elem);

  free(pte);
}

void page_table_entry_destroy(struct hash_elem *elem, void *aux) {
    struct page_table_entry *pte = hash_entry(elem, struct page_table_entry, hash_elem);
    if (pte->disk) {
      // swap
      lock_acquire(&swap_lock);
      bitmap_set(swap_table, pte->block / block_size, true);
      lock_release(&swap_lock);
    } else {
      // frame
      struct frame_table_entry *fte = frame_table_find(pte->frame);
      hash_delete(&frame_table.hash, &fte->hash_elem);
      free(fte);
    }
    free(pte);
}

void page_table_free(struct page_table *page_table) {
  #ifdef DEBUG
  printf("[page_table_free] pid: %d\n", process_current()->pid);
  #endif

  hash_destroy(&page_table->hash, page_table_entry_destroy);
}

/*
 * Make new supplementary page table entry for addr
 */
/*
struct sup_page_table_entry *
allocate_page (void *addr)
{

}
*/
