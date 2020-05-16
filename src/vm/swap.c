#include <bitmap.h>
#include "vm/swap.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

/* The swap device */
static struct disk *swap_device;

/* Tracks in-use and free swap slots */
struct bitmap *swap_table;

/* Protects swap_table */
struct lock swap_lock;

const int block_size = PGSIZE / DISK_SECTOR_SIZE;

/*
 * Initialize swap_device, swap_table, and swap_lock.
 */
void
swap_table_init (void)
{
    #ifdef DEBUG
    printf("[swap_table_init]\n");
    #endif

    swap_device = disk_get(1, 1);
    ASSERT(swap_device != NULL);
    swap_table = bitmap_create(disk_size(swap_device) / block_size);
    lock_init(&swap_lock);
}

/*
 * Reclaim a frame from swap device.
 * 1. Check that the page has been already evicted.
 * 2. You will want to evict an already existing frame
 * to make space to read from the disk to cache.
 * 3. Re-link the new frame with the corresponding supplementary
 * page table entry.
 * 4. Do NOT create a new supplementray page table entry. Use the
 * already existing one.
 * 5. Use helper function read_from_disk in order to read the contents
 * of the disk into the frame.
 */
void
swap_in (disk_sector_t block, uint8_t *frame)
{
    #ifdef DEBUG
    printf("[swap_in] block: %u, frame: %x\n", block, frame);
    #endif

    // prevent concurrent disk access
    lock_acquire(&swap_lock);
    int i;
    for (i=0; i<block_size; i++) {
        disk_read(swap_device, block + i, frame + i * DISK_SECTOR_SIZE);
    }
    bitmap_set(swap_table, block / block_size, false);
    lock_release(&swap_lock);

}

/*
 * Evict a frame to swap device.
 * 1. Choose the frame you want to evict.
 * (Ex. Least Recently Used policy -> Compare the timestamps when each
 * frame is last accessed)
 * 2. Evict the frame. Unlink the frame from the supplementray page table entry
 * Remove the frame from the frame table after freeing the frame with
 * pagedir_clear_page.
 * 3. Do NOT delete the supplementary page table entry. The process
 * should have the illusion that they still have the page allocated to
 * them.
 * 4. Find a free block to write you data. Use swap table to get track
 * of in-use and free swap slots.
 */
disk_sector_t
swap_out (uint8_t *frame)
{
    #ifdef DEBUG
    printf("[swap_out] frame: %x\n", frame);
    #endif

    lock_acquire(&swap_lock);
    size_t temp = bitmap_scan_and_flip(swap_table, 0, 1, false);
    if (temp == BITMAP_ERROR) {
        // kernel panic
    }
    disk_sector_t block = temp * block_size;
    int i;
    for (i=0; i<block_size; i++) {
        disk_write(swap_device, block + i, frame + i * DISK_SECTOR_SIZE);
    }
    lock_release(&swap_lock);

    return block;
}
