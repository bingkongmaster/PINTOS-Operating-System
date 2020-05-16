#include "vm/frame.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"

struct frame_table frame_table;
extern struct lock lock_file;

unsigned frame_hash (const struct hash_elem *e, void *aux);
bool frame_less (const struct hash_elem *a, const struct hash_elem *b, void *aux);

unsigned frame_hash (const struct hash_elem *e, void *aux) {
    struct frame_table_entry *fte = hash_entry(e, struct frame_table_entry, hash_elem);
    return hash_bytes(&fte->frame, sizeof(fte->frame));
}

bool frame_less (const struct hash_elem *a, const struct hash_elem *b, void *aux) {
    struct frame_table_entry *fte1 = hash_entry(a, struct frame_table_entry, hash_elem);
    struct frame_table_entry *fte2 = hash_entry(b, struct frame_table_entry, hash_elem);
    return fte1->frame < fte2->frame;
}

/*
 * Initialize frame table
 */
void
frame_table_init (void)
{
    #ifdef DEBUG
    printf("[frame_table_init]\n");
    #endif

    lock_init(&frame_table.lock);
    hash_init(&frame_table.hash, frame_hash, frame_less, NULL);
    list_init(&frame_table.list);
}

void frame_table_insert(struct process *process, uint8_t *frame, uint8_t *page) {
    #ifdef DEBUG
    printf("[frame_table_insert] pid: %d, frame: %x, page: %x\n", process->pid, frame, page);
    #endif

    struct frame_table_entry *fte = frame_table_find(frame);
    if (fte == NULL) {
        fte = (struct frame_table_entry *)malloc(sizeof(struct frame_table_entry));
    }
    fte->frame = frame;
    fte->owner = process;
    fte->page = page;

    // insert to frame table, consider replacement
    hash_replace(&frame_table.hash, &fte->hash_elem);
    list_push_back(&frame_table.list, &fte->list_elem);
}

void frame_table_remove(uint8_t *frame) {
    #ifdef DEBUG
    printf("[frame_table_remove] frame: %x\n", frame);
    #endif

    struct frame_table_entry *fte = frame_table_find(frame);
    hash_delete(&frame_table.hash, &fte->hash_elem);
    list_remove(&fte->list_elem);

    free(fte);
}

struct frame_table_entry *frame_table_find(uint8_t *frame) {
    #ifdef DEBUG
    printf("[frame_table_find] frame: %x\n", frame);
    #endif

    struct frame_table_entry fte;
    fte.frame = frame;
    struct hash_elem *e = hash_find(&frame_table.hash, &fte.hash_elem);
    if (e == NULL) {
        return NULL;
    }
    return hash_entry(e, struct frame_table_entry, hash_elem);
}

uint8_t *frame_table_select_victim() {
    // get page table entry according to owener and page

    /*
    // WARNING: current implementation returns the first hash element
    struct hash_iterator i;
    hash_first(&i, &frame_table.hash);
    hash_next(&i);
    struct frame_table_entry *fte = hash_entry(hash_cur(&i), struct frame_table_entry, hash_elem);
    uint8_t *frame = fte->frame;
    */

    // WARNING: FIFO
    struct list_elem *e = list_pop_front(&frame_table.list);
    struct frame_table_entry *fte = list_entry(e, struct frame_table_entry, list_elem);
    uint8_t *frame = fte->frame;
    list_push_back(&frame_table.list, e);

    /*
    struct list_elem *e;
    uint8_t *frame = list_back(&frame_table.list);
    for (e=list_begin(&frame_table.list); e!=list_end(&frame_table.list); e=list_next(e)) {
      struct frame_table_entry *fte = list_entry(e, struct frame_table_entry, list_elem);
      if (!(pagedir_is_dirty(fte->owner->thread->pagedir, fte->page) ||
        pagedir_is_accessed(fte->owner->thread->pagedir, fte->page))) {
          frame = fte->frame;
          break;
      }
    }
    */

    #ifdef DEBUG
    printf("[frame_table_select_victim] victim: %x\n", frame);
    #endif

    return frame;
}

/*
 * Make a new frame table entry for addr.
 * allocate_frame does not call palloc_get_page.
 */
uint8_t *
allocate_frame (uint8_t *page, bool writable, enum palloc_flags flag)
{
  #ifdef DEBUG
  printf("[allocate_frame] page: %x\n", page);
  #endif

  // prevent concurrent table modifications
  lock_acquire(&frame_table.lock);
  uint8_t *frame = palloc_get_page(PAL_USER | flag);

  if (frame == NULL) {
    // select victim frame
    frame = frame_table_select_victim();

    // fill in pte
    pagedir_clear_page(thread_current()->pagedir, page);
    pagedir_set_page(thread_current()->pagedir, page, frame, writable);

    struct frame_table_entry *vfte = frame_table_find(frame);
    struct page_table_entry *vpte = page_table_find(&vfte->owner->page_table, vfte->page);

    // victim frame mapped to file
    if (vpte->file) {
      // file out
      lock_acquire(&lock_file);
      file_write_at(vpte->file, vpte->frame, PGSIZE, vpte->offset);
      lock_release(&lock_file);

      // update page table for file out page(process)
      page_table_insert_file(&vfte->owner->page_table, vfte->page, vpte->file, vpte->offset);
    } else {
        // swap out, what if disk full?
        disk_sector_t block = swap_out(frame);

        // update page table for swapped out page(process)
        page_table_insert_block(&vfte->owner->page_table, vfte->page, block);
    }

    pagedir_clear_page(vfte->owner->thread->pagedir, vfte->page); // clear page

    // update frame table and page table for current process
    frame_table_insert(process_current(), frame, page);
    page_table_insert_frame(&process_current()->page_table, page, frame);
  } else {
    // fill in pte
    pagedir_clear_page(thread_current()->pagedir, page);
    pagedir_set_page(thread_current()->pagedir, page, frame, writable);

    // update frame table and page table for current process
    frame_table_insert(process_current(), frame, page);
    page_table_insert_frame(&process_current()->page_table, page, frame);
  }
  lock_release(&frame_table.lock);

  return frame;
}
