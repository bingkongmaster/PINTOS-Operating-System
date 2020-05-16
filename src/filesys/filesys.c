#include "filesys/filesys.h"
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "devices/disk.h"

#ifdef PR_FS
#include "userprog/process.h"
#include "filesys/cache.h"
#include "threads/malloc.h"

#define DEPTH_MAX 128
#endif

/* The disk that contains the file system. */
struct disk *filesys_disk;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format)
{
  filesys_disk = disk_get (0, 1);
  if (filesys_disk == NULL)
    PANIC ("hd0:1 (hdb) not present, file system initialization failed");

  inode_init ();
  free_map_init ();

  #ifdef PR_FS
  cache_table_init();
  // initialize unused array
  int i;
  for (i=0; i<DISK_SECTOR_SIZE; i++) {
      unused[i] = UNUSED_SECTOR;
  }
  lock_init(&inode_lock);
  #endif

  if (format)
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void)
{
  #ifdef PR_FS
  cache_table_destroy();
  #endif
  free_map_close ();
}


#ifdef PR_FS
void filesys_parse(char **path, const char *name, int *depth) {
  char *next;
  char delim[] = "/";
  path[*depth] = strtok_r((char *)name, delim, &next);
  while (path[*depth]) {
    *depth += 1;
    path[*depth] = strtok_r((char *)NULL, delim, &next);
  }
}

struct dir *filesys_find_dir(struct dir *dir, struct inode *inode, const char *name, char **final) {
  // absolute path
  int depth = 0;
  char **path = (char **)calloc(DEPTH_MAX, sizeof(char *));
  filesys_parse(path, name, &depth);

  // search by Iteration
  dir = dir_reopen(dir);
  int i;
  for (i=0; i<depth-1; i++) { // directory traverse
    if (!dir_lookup(dir, path[i], &inode)) {
      // couldn't find file or directory
      dir_close(dir);
      free(path);
      return NULL;
    } else if (!inode->data.is_dir) {
      // found file
      dir_close(dir);
      inode_close(inode);
      free(path);
      return NULL;
    } else {
      // found directory
      dir_close(dir);
      dir = dir_open(inode);
      if (!dir) {
        free(path);
        return NULL;
      }
    }
  }

  static char empty[] = "";
  if (depth == 0) {
    *final = empty;
  } else {
    *final = path[depth - 1];
  }

  free(path);
  return dir;
  // final search being done after this call
}

bool filesys_find_and_create(struct dir *dir, struct inode *inode, const char *name, off_t initial_size) {
  #ifdef DEBUG
  printf("[filesys_find_and_create] dir: %x, inode: %x, name: %s, initial_size: %u\n", dir, inode, name, initial_size);
  #endif

  char *final; // final file name
  dir = filesys_find_dir(dir, inode, name, &final); // parent of final file
  if (dir == NULL || dir->inode->removed) {
    // search failed
    dir_close(dir);
    return false;
  }

  #ifdef DEBUG
  printf("final: %s, inode: %x\n", final, dir->inode);
  #endif

  // final creation
  disk_sector_t inode_sector = 0;
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, false, dir->inode->sector)
                  && dir_add (dir, final, inode_sector));
  if (!success && inode_sector != 0)
    free_map_release (inode_sector, 1);

  dir_close (dir);

  return success;
}

struct file *filesys_find_and_open(struct dir *dir, struct inode *inode, const char *name) {
  #ifdef DEBUG
  printf("[filesys_find_and_open] dir: %x, inode: %x, name: %s\n", dir, inode, name);
  #endif

  char *final; // final file name
  dir = filesys_find_dir(dir, inode, name, &final); // parent of final file
  if (dir == NULL) {
    // search failed
    dir_close(dir);
    return NULL;
  }

  #ifdef DEBUG
  printf("final: %s, inode: %x, length: %u\n", final, dir->inode, dir->inode->data.length);
  #endif

  // final search
  if (!dir_lookup(dir, final, &inode)) {
    // final search failed
    dir_close(dir);
    return NULL;
  }
  dir_close(dir);
  return file_open(inode);
}

 bool filesys_find_and_remove(struct dir *dir, struct inode *inode, const char *name) {
   char *final; // final file name
   dir = filesys_find_dir(dir, inode, name, &final); // parent of final file

   if (dir == NULL) {
     // search failed
     dir_close(dir);
     return false;
   }
   // final removal
   if (!dir_remove(dir, final)) {
     // final search failed
     dir_close(dir);
     return false;
   }
   dir_close(dir);
   return true;
}


#endif

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size)
{
  #ifdef PR_FS
  struct dir *dir;
  struct inode *inode = NULL;
  // distinguish two cases: 1. absolute 2. relative
  if (name[0] == '/') {
    // absolute path
    dir = dir_open_root();
    inode = dir->inode;
  } else {
    // relative path
    dir = process_current_dir();
    inode = dir->inode;
  }
  return filesys_find_and_create(dir, inode, name, initial_size);
  #else
  disk_sector_t inode_sector = 0;
  struct dir *dir = dir_open_root ();
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size)
                  && dir_add (dir, name, inode_sector));
  if (!success && inode_sector != 0)
    free_map_release (inode_sector, 1);
  dir_close (dir);

  return success;
  #endif
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  #ifdef PR_FS
  struct dir *dir;
  struct inode *inode = NULL;
  // distinguish two cases: 1. absolute 2. relative
  if (name[0] == '/') {
    // absolute path
    dir = dir_open_root();
    inode = dir->inode;
  } else {
    // relative path
    dir = process_current_dir();
    inode = dir->inode;
  }
  return filesys_find_and_open(dir, inode, name);
  #else
  struct dir *dir = dir_open_root ();
  struct inode *inode = NULL;

  if (dir != NULL)
    dir_lookup (dir, name, &inode);
  dir_close (dir);

  return file_open (inode);

  #endif
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name)
{
  #ifdef PR_FS
  struct dir *dir;
  struct inode *inode = NULL;
  // distinguish two cases: 1. absolute 2. relative
  if (name[0] == '/') {
    // absolute path
    dir = dir_open_root();
    inode = dir->inode;
  } else {
    // relative path
    dir = process_current_dir();
    inode = dir->inode;
  }
  return filesys_find_and_remove(dir, inode, name);
  #else
  struct dir *dir = dir_open_root ();
  bool success = dir != NULL && dir_remove (dir, name);
  dir_close (dir);
  printf("success: %d\n", success);
  return success;
  #endif
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16, UNUSED_SECTOR))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
