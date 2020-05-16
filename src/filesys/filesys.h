#ifndef FILESYS_FILESYS_H
#define FILESYS_FILESYS_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include <debug.h>

#ifdef PR_FS
#include "filesys/inode.h"
#endif

/* Sectors of system file inodes. */
#define FREE_MAP_SECTOR 0       /* Free map file inode sector. */
#define ROOT_DIR_SECTOR 1       /* Root directory file inode sector. */

/* Disk used for file system. */
extern struct disk *filesys_disk;

void filesys_init (bool format);
void filesys_done (void);
bool filesys_create (const char *name, off_t initial_size);
struct file *filesys_open (const char *name);
bool filesys_remove (const char *name);

#ifdef PR_FS
void filesys_parse(char **path, const char *name, int *depth);
struct dir *filesys_find_dir(struct dir *dir, struct inode *inode, const char *name, char **final);
bool filesys_find_and_create(struct dir *dir, struct inode *inode, const char *name, off_t iniitial_size);
struct file *filesys_find_and_open(struct dir *dir, struct inode *inode, const char *name);
bool filesys_find_and_remove(struct dir *dir, struct inode *inode, const char *name);
#endif
#endif /* filesys/filesys.h */
