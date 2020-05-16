#ifndef VM_SWAP_H
#define VM_SWAP_H

#include "devices/disk.h"

void swap_table_init (void);

void swap_in(disk_sector_t block, uint8_t *frame);
disk_sector_t swap_out(uint8_t *frame);

#endif /* vm/swap.h */
