#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include <debug.h>
#include "filesys/off_t.h"
#include "devices/disk.h"
#include "threads/synch.h"

#ifdef PR_FS
#define UNUSED_SECTOR (disk_sector_t)-1

#define DIRECT_MAX 12 // maximum number of direct blocks in inode structure
#define SECTOR_MAX 128 // maximum number of sectors in one disk sector

extern disk_sector_t unused[DISK_SECTOR_SIZE];
extern struct lock inode_lock;
#endif

struct bitmap;

/* On-disk inode.
   Must be exactly DISK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    #ifdef PR_FS
    disk_sector_t direct[DIRECT_MAX];
    disk_sector_t indirect;
    disk_sector_t double_indirect;
    int is_dir; // is inode directory?
    disk_sector_t parent_dir; // parent directory
    #endif
    // disk_sector_t start;                /* First data sector. */
    off_t length;                       /* File size in bytes. */

    unsigned magic;                     /* Magic number. */
    uint32_t unused[110];               /* Not used. */
  };

/* In-memory inode. */
struct inode
  {
    struct list_elem elem;              /* Element in inode list. */
    disk_sector_t sector;               /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
  };

void inode_init (void);
bool inode_create (disk_sector_t, off_t, bool, disk_sector_t);
struct inode *inode_open (disk_sector_t);
struct inode *inode_reopen (struct inode *);
disk_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);

#endif /* filesys/inode.h */
