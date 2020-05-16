#include "filesys/inode.h"
#include <list.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

#ifdef PR_FS
#include "filesys/cache.h"
#endif

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, DISK_SECTOR_SIZE);
}


#ifdef PR_FS
disk_sector_t unused[DISK_SECTOR_SIZE]; // initialized to UNUSED_SECTOR
struct lock inode_lock;
static disk_sector_t allocate_block(struct inode_disk *data, unsigned pos);
static void free_blocks(struct inode_disk *data);
#endif


/* Returns the disk sector that contains byte offset POS within
   INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
/*
static disk_sector_t
byte_to_sector (const struct inode *inode, off_t pos)
{
  ASSERT (inode != NULL);
  if (pos < inode->data.length)
    return inode->data.start + pos / DISK_SECTOR_SIZE;
  else
    return -1;
}
*/

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

#ifdef PR_FS

disk_sector_t allocate_block(struct inode_disk *data, unsigned pos) {
  disk_sector_t indirect;
  disk_sector_t block;

  static char zeros[DISK_SECTOR_SIZE];

  // printf("[allocate block] pos: %u\n", pos);
  if (pos < DIRECT_MAX) {
    // direct block
    if (data->direct[pos] == UNUSED_SECTOR) {
      if (!free_map_allocate(1, &data->direct[pos])) {
        return UNUSED_SECTOR;
      }
      cache_table_write(zeros, data->direct[pos], 0, DISK_SECTOR_SIZE);
    }
    block = data->direct[pos];
  } else if (pos < DIRECT_MAX + SECTOR_MAX) {
    // indirect block
    if (data->indirect == UNUSED_SECTOR) {
      // indirect block does not exist
      if(!free_map_allocate(1, &data->indirect)) {
        return UNUSED_SECTOR;
      }
      cache_table_write((uint8_t *)unused, data->indirect, 0, DISK_SECTOR_SIZE);
    }

    int doffset = pos - DIRECT_MAX;

    cache_table_read((uint8_t *)&block, data->indirect, doffset * sizeof(disk_sector_t), sizeof(disk_sector_t));
    if (block == UNUSED_SECTOR) {
      if (!free_map_allocate(1, &block)) {
        return UNUSED_SECTOR;
      }
      cache_table_write((uint8_t *)&block, data->indirect, doffset * sizeof(disk_sector_t), sizeof(disk_sector_t));
      cache_table_write(zeros, block, 0, DISK_SECTOR_SIZE);
    }
  } else {
    // double indirect block
    if (data->double_indirect == UNUSED_SECTOR) {
      // double indirect block does not exist
      if (!free_map_allocate(1, &data->double_indirect)) {
        return UNUSED_SECTOR;
      }
      cache_table_write((uint8_t *)unused, data->double_indirect, 0, DISK_SECTOR_SIZE);
    }

    int ioffset = (pos - DIRECT_MAX - SECTOR_MAX) / SECTOR_MAX;
    int doffset = (pos - DIRECT_MAX - SECTOR_MAX) % SECTOR_MAX;

    cache_table_read((uint8_t *)&indirect, data->double_indirect, ioffset * sizeof(disk_sector_t), sizeof(disk_sector_t));
    if (indirect == UNUSED_SECTOR) {
      if (!free_map_allocate(1, &indirect)) {
        return UNUSED_SECTOR;
      }
      cache_table_write((uint8_t *)&indirect, data->double_indirect, ioffset * sizeof(disk_sector_t), sizeof(disk_sector_t));
      cache_table_write((uint8_t *)unused, indirect, 0, DISK_SECTOR_SIZE);
    }

    cache_table_read((uint8_t *)&block, indirect, doffset * sizeof(disk_sector_t), sizeof(disk_sector_t));
    if (block == UNUSED_SECTOR) {
      if (!free_map_allocate(1, &block)) {
        return UNUSED_SECTOR;
      }
      cache_table_write((uint8_t *)&block, indirect, doffset * sizeof(disk_sector_t), sizeof(disk_sector_t));
      cache_table_write(zeros, block, 0, DISK_SECTOR_SIZE);
    }
  }
  return block;
}

void free_blocks(struct inode_disk *data) {
  // how to free blocks?
  // blocks from 1 ~ length divided by DISK_SECTOR_SIZE
  // don't know which one is actually in use so just iterate through all and free

  int pos;
  int sectors = bytes_to_sectors(data->length);

  disk_sector_t indirect;
  disk_sector_t block;

  for (pos=sectors-1; pos>=0; pos--) {
    if (pos < DIRECT_MAX) {
      // free direct block
      free_map_release(data->direct[pos], 1);
      free_cache(data->direct[pos]);
    } else if (pos < DIRECT_MAX + SECTOR_MAX) {
      // free indirect block
      int doffset = pos - DIRECT_MAX;

      cache_table_read((uint8_t *)&block, data->indirect, doffset * sizeof(disk_sector_t), sizeof(disk_sector_t));
      if (block != UNUSED_SECTOR) {
        free_map_release(block, 1);
        free_cache(block);
      }
      if (doffset == 0) {
        free_map_release(data->indirect, 1);
        free_cache(data->indirect);
      }
    } else {
      // free double indirect block
      int ioffset = (pos - DIRECT_MAX - SECTOR_MAX) / SECTOR_MAX;
      int doffset = (pos - DIRECT_MAX - SECTOR_MAX) % SECTOR_MAX;

      cache_table_read((uint8_t *)&indirect, data->double_indirect, ioffset * sizeof(disk_sector_t), sizeof(disk_sector_t));
      if (indirect != UNUSED_SECTOR) {
        cache_table_read((uint8_t *)&block, indirect, doffset * sizeof(disk_sector_t), sizeof(disk_sector_t));
        if (block != UNUSED_SECTOR) {
          free_map_release(block, 1);
          free_cache(block);
        }
        if (doffset == 0) {
          free_map_release(indirect, 1);
          free_cache(indirect);
        }
      }

      if (ioffset == 0 && doffset == 0) {
        free_map_release(data->double_indirect, 1);
        free_cache(data->double_indirect);
      }
    }
  }
}
#endif

/* Initializes the inode module. */
void
inode_init (void)
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   disk.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (disk_sector_t sector, off_t length, bool is_dir, disk_sector_t parent_dir)
{
  #ifdef DEBUG
  printf("[inode_create ENTRY] sector: %u, length: %u\n", sector, length);
  #endif
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == DISK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      size_t sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;

      #ifdef PR_FS
      // put in direct, indirect, double indirect blocks into inode_disk
      int i;
      for (i=0; i<DIRECT_MAX; i++) {
        disk_inode->direct[i] = UNUSED_SECTOR;
      }

      disk_inode->indirect = UNUSED_SECTOR;
      disk_inode->double_indirect = UNUSED_SECTOR;

      // put in directory information
      disk_inode->is_dir = is_dir;
      disk_inode->parent_dir = parent_dir;

      success = true;
      for (i=0; i<sectors; i++) {
        if (allocate_block(disk_inode, i) == UNUSED_SECTOR) {
          success = false;
        }
      }
      #ifdef DEBUG
      printf("[inode_create MEANWHILE] success: %u, sectors: %d\n", success, sectors);
      #endif
      if (success) {
        disk_write(filesys_disk, sector, disk_inode);
      } else {
        // WARNING: need to free allocated blocks when failed
      }
      #else
      if (free_map_allocate (sectors, &disk_inode->start))
        {
          disk_write (filesys_disk, sector, disk_inode);
          if (sectors > 0)
            {
              static char zeros[DISK_SECTOR_SIZE];
              size_t i;

              for (i = 0; i < sectors; i++)
                disk_write (filesys_disk, disk_inode->start + i, zeros);
            }
          success = true;
        }
      #endif
      free (disk_inode);
    }

  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (disk_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e))
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector)
        {
          inode_reopen (inode);
          return inode;
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  disk_read (filesys_disk, inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
disk_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode)
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);

      /* Deallocate blocks if removed. */
      if (inode->removed)
        {
          free_map_release (inode->sector, 1);
          #ifdef PR_FS
          free_blocks(&inode->data);
          #else
          free_map_release (inode->data.start,
                            bytes_to_sectors (inode->data.length));
          #endif
        } else {
          // write back
          disk_write(filesys_disk, inode->sector, &inode->data);
        }

      free (inode);
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode)
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset)
{
  #ifdef DEBUG
  printf("[inode_read_at] inode: %x, buffer: %x, size: %u, offset: %u\n", inode, buffer_, size, offset);
  #endif

  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0)
    {
      /* Disk sector to read, starting byte offset within sector. */
      disk_sector_t sector_idx = allocate_block(&inode->data, offset / DISK_SECTOR_SIZE);
      int sector_ofs = offset % DISK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = DISK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      #ifdef PR_FS
      cache_table_read(buffer + bytes_read, sector_idx, sector_ofs, chunk_size);
      #else
      if (sector_ofs == 0 && chunk_size == DISK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          disk_read (filesys_disk, sector_idx, buffer + bytes_read);
        }
      else
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL)
            {
              bounce = malloc (DISK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          disk_read (filesys_disk, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }
      #endif

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset)
{
  #ifdef DEBUG
  printf("[inode_write_at] inode: %x, buffer: %x, size: %u, offset: %u\n", inode, buffer_, size, offset);
  #endif

  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  if (inode->data.length < offset + size) {
      inode->data.length = offset + size;
  }

  while (size > 0)
    {
      /* Sector to write, starting byte offset within sector. */
      disk_sector_t sector_idx = allocate_block(&inode->data, offset / DISK_SECTOR_SIZE);
      int sector_ofs = offset % DISK_SECTOR_SIZE;

      #ifdef PR_FS
      // file growth
      int sector_left = DISK_SECTOR_SIZE - sector_ofs;
      int chunk_size = size < sector_left ? size : sector_left;
      if (chunk_size <= 0)
        break;
      #else
      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = DISK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;
      #endif

      #ifdef PR_FS
      cache_table_write((uint8_t *)buffer + bytes_written, sector_idx, sector_ofs, chunk_size);
      #else
      if (sector_ofs == 0 && chunk_size == DISK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          disk_write (filesys_disk, sector_idx, buffer + bytes_written);
        }
      else
        {
          /* We need a bounce buffer. */
          if (bounce == NULL)
            {
              bounce = malloc (DISK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left)
            disk_read (filesys_disk, sector_idx, bounce);
          else
            memset (bounce, 0, DISK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          disk_write (filesys_disk, sector_idx, bounce);
        }
      #endif

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode)
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode)
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}
