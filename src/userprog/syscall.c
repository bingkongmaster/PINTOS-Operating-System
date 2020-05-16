#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

#ifdef PR_USER
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "threads/init.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/input.h"
#include <string.h>

#ifdef PR_FS
#include "filesys/directory.h"
#include "filesys/inode.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#endif

#define SYSCALL_COUNT 20

extern struct lock lock_file;

#ifdef PR_VM
extern struct frame_table frame_table;
#endif

bool is_valid(void *uaddr, void *esp);
void *get_pointer(void *esp, int index);
int get_integer(void *esp, int index);
void exit_status(int status);

void sys_halt(struct intr_frame *f);
void sys_exit(struct intr_frame *f);
void sys_exec(struct intr_frame *f);
void sys_wait(struct intr_frame *f);
void sys_create(struct intr_frame *f);
void sys_remove(struct intr_frame *f);
void sys_open(struct intr_frame *f);
void sys_filesize(struct intr_frame *f);
void sys_read(struct intr_frame *f);
void sys_write(struct intr_frame *f);
void sys_seek(struct intr_frame *f);
void sys_tell(struct intr_frame *f);
void sys_close(struct intr_frame *f);

#ifdef PR_VM
void sys_mmap(struct intr_frame *f);
void sys_munmap(struct intr_frame *f);
#endif

#ifdef PR_FS
void sys_chdir(struct intr_frame *f);
void sys_mkdir(struct intr_frame *f);
void sys_readdir(struct intr_frame *f);
void sys_isdir(struct intr_frame *f);
void sys_inumber(struct intr_frame *f);
#endif

#endif

static void syscall_handler (struct intr_frame *);

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

#ifdef PR_USER

/**
 * User memory access
 * Check whether if uaddr is pointing to the valid user memory
 */
bool is_valid(void *uaddr, void *esp) {

  // reject accessing kernel memory
  if (is_kernel_vaddr(uaddr)) {
    return false;
  }

  #ifdef PR_VM
  uint8_t *page = pg_round_down(uaddr);
  if (!(page_table_find(&process_current()->page_table, page) || (uint8_t *)esp <= page)) {
    return false;
  }
  #else
  // not needed for VM, page fault handler will handle
  uint32_t *pd = thread_current()->pagedir;

  if (!pagedir_get_page(pd, uaddr)) {
    return false;
  }
  #endif

  return true;
}

void *get_pointer(void *esp, int index) {
  if (!is_valid((void **)esp + index, esp)) {
    exit_status(PID_ERROR);
  }

  void *ptr = *((void **)esp + index);
  if (is_valid(ptr, esp)) {
    return ptr;
  } else {
    exit_status(PID_ERROR);
  }

  NOT_REACHED()
}

int get_integer(void *esp, int index) {
  if (!is_valid((int *)esp + index, esp)) {
    exit_status(PID_ERROR);
  }

  return *((int *)esp + index);
}

void exit_status(int status) {
  struct process *p = process_current();

  // release resources (lock)
  // memory is released after reaped

  // terminates the current process
  p->status = status;
  thread_exit();
}


void sys_halt(struct intr_frame *f UNUSED) {
  power_off();
}

void sys_exit(struct intr_frame *f) {
  int status = get_integer(f->esp, 1);

  #ifdef DEBUG
  printf("[sys_exit] status: %d\n", status);
  #endif

  // exit status might be needed for waiting parent
  exit_status(status);
}

void sys_exec(struct intr_frame *f) {
  const char *cmd_line = get_pointer(f->esp, 1);

  #ifdef DEBUG
  printf("[sys_exec] cmd_line: %s\n", cmd_line);
  #endif

  f->eax = process_execute(cmd_line);
}

void sys_wait(struct intr_frame *f) {
  pid_t pid = get_integer(f->esp, 1);

  #ifdef DEBUG
  printf("[sys_exec] pid: %d\n", pid);
  #endif

  f->eax = process_wait(pid);
}

void sys_create(struct intr_frame *f) {
  const char *name = get_pointer(f->esp, 1);
  unsigned int initial_size = get_integer(f->esp, 2);

  #ifdef DEBUG
  printf("[sys_create] name: %s, initial_size: %d\n", name, initial_size);
  #endif

  if (!name || !strcmp(name, "")) {
    f->eax = 0;
    return;
  }

  char *temp = (char *)malloc(sizeof(char) * (NAME_MAX + 1));
  strlcpy(temp, name, strlen(name) + 1);

  lock_acquire(&lock_file);
  f->eax = filesys_create(temp, initial_size);
  lock_release(&lock_file);
  free(temp);
}

void sys_remove(struct intr_frame *f) {
  const char *name = get_pointer(f->esp, 1);

  #ifdef DEBUG
  printf("[sys_remove] name: %s\n", name);
  #endif

  if (!name || !strcmp(name, "")) {
    f->eax = 0;
    return;
  }

  char *temp = (char *)malloc(sizeof(char) * (NAME_MAX + 1));
  strlcpy(temp, name, strlen(name) + 1);

  lock_acquire(&lock_file);
  f->eax = filesys_remove(temp);
  lock_release(&lock_file);

  free(temp);
}

void sys_open(struct intr_frame *f) {
  const char *name = get_pointer(f->esp, 1);

  #ifdef DEBUG
  printf("[sys_open] name: %s\n", name);
  #endif

  if (!name || !strcmp(name, "")) {
    f->eax = -1;
    return;
  }

  char *temp = (char *)malloc(sizeof(char) * (NAME_MAX + 1));
  strlcpy(temp, name, strlen(name) + 1);

  lock_acquire(&lock_file);
  struct file *file = filesys_open(temp);
  lock_release(&lock_file);
  if (!file) {
    free(temp);
    f->eax = -1;
    return;
  }
  free(temp);
  f->eax = process_open_file(file);
}

void sys_filesize(struct intr_frame *f) {
  int fd = get_integer(f->esp, 1);

  #ifdef DEBUG
  printf("[sys_filesize] fd: %d\n", fd);
  #endif

  if (fd == STDIN_FILENO) {
    f->eax = -1;
    return;
  } else if (fd == STDOUT_FILENO) {
    f->eax = -1;
    return;
  } else {
    struct process *p = process_current();
    if (!process_valid_fd(fd)) {
      f->eax = -1;
      return;
    }
    lock_acquire(&lock_file);
    f->eax = file_length(p->files[fd]);
    lock_release(&lock_file);
  }
}

void sys_read(struct intr_frame *f) {
  int fd = get_integer(f->esp, 1);
  void *buffer = get_pointer(f->esp, 2);
  unsigned int size = (unsigned int)get_integer(f->esp, 3);

  #ifdef DEBUG
  printf("[sys_read] fd: %d, buffer: %x, size: %u\n", fd, buffer, size);
  #endif

  if (fd == STDIN_FILENO) {
    f->eax = size;
    while (size-- > 0) {
      input_getc();
    }
  } else if (fd == STDOUT_FILENO) {
    f->eax = -1;
    return;
  } else {
    struct process *p = process_current();
    if (!process_valid_fd(fd) || p->files[fd]->inode->data.is_dir) {
      f->eax = -1;
      return;
    }
    lock_acquire(&lock_file);
    f->eax = file_read(p->files[fd], buffer, size);
    lock_release(&lock_file);
  }
}

void sys_write(struct intr_frame *f) {
  int fd = get_integer(f->esp, 1);
  const void *buffer = get_pointer(f->esp, 2);
  unsigned int size = (unsigned int)get_integer(f->esp, 3);

  #ifdef DEBUG
  printf("[sys_write] fd: %d, buffer %x, size: %u\n", fd, buffer, size);
  #endif

  if (fd == STDIN_FILENO) {
    f->eax = -1;
    return;
  } else if (fd == STDOUT_FILENO) {
    putbuf((const char *)buffer, size);
    // break the buffer if the size is too big
    f->eax = size;
  } else {
    struct process *p = process_current();
    if (!process_valid_fd(fd) || p->files[fd]->inode->data.is_dir) {
      f->eax = -1;
      return;
    }
    lock_acquire(&lock_file);
    f->eax = file_write(p->files[fd], buffer, size);
    lock_release(&lock_file);
  }
}

void sys_seek(struct intr_frame *f) {
  int fd = get_integer(f->esp, 1);
  unsigned int position = get_integer(f->esp, 2);

  #ifdef DEBUG
  printf("[sys_seek] fd: %d, position: %d\n", fd, position);
  #endif

  if (fd == STDIN_FILENO) {
    return;
  } else if (fd == STDOUT_FILENO) {
    return;
  } else {
    struct process *p = process_current();
    if (!process_valid_fd(fd)) {
      return;
    }
    lock_acquire(&lock_file);
    file_seek(p->files[fd], position);
    lock_release(&lock_file);
  }
}

void sys_tell(struct intr_frame *f) {
  int fd = get_integer(f->esp, 1);

  #ifdef DEBUG
  printf("[sys_tell] fd: %d\n", fd);
  #endif

  if (fd == STDIN_FILENO) {
    f->eax = -1;
    return;
  } else if (fd == STDOUT_FILENO) {
    f->eax = -1;
    return;
  } else {
    struct process *p = process_current();
    if (!process_valid_fd(fd)) {
      f->eax = -1;
      return;
    }
    lock_acquire(&lock_file);
    f->eax = file_tell(p->files[fd]);
    lock_release(&lock_file);
  }
}

void sys_close(struct intr_frame *f) {
  int fd = get_integer(f->esp, 1);

  #ifdef DEBUG
  printf("[sys_close] fd: %d\n", fd);
  #endif

  struct process *p = process_current();
  if (!process_valid_fd(fd)) {
    return;
  }

  lock_acquire(&lock_file);
  /*
  if (!p->files[fd]->inode->data.is_dir) {
    // close file
    file_close(p->files[fd]);
  } else {
    // close directory
    dir_close(p->files[fd]);
  }
  */
  file_close(p->files[fd]);
  lock_release(&lock_file);
  p->files[fd] = NULL;
}

#ifdef PR_VM
void sys_mmap(struct intr_frame *f) {
  int fd = get_integer(f->esp, 1);
  uint8_t *page = (uint8_t *)*((void **)f->esp + 2); // should not use get_pointer

  #ifdef DEBUG
  printf("[sys_mmap] fd: %d, page: %x\n", fd, page);
  #endif

  // prevent concurrent page table access

  struct process *p = process_current();
  if (!p->files[fd]) {
    f->eax = -1;
    return;
  }
  lock_acquire(&frame_table.lock);
  lock_acquire(&lock_file);

  off_t len = file_length(p->files[fd]);

  // validity check
  if (fd <= STDOUT_FILENO ||
      !len ||
      !page ||
      page != pg_round_down(page)) {

    lock_release(&lock_file);
    lock_release(&frame_table.lock);
    f->eax = -1;
    return;
  }

  int i;
  // validity check
  for (i=0; i<len / PGSIZE + 1; i++) {
    if (page_table_find(&p->page_table, page + i * PGSIZE)) {
      lock_release(&lock_file);
      lock_release(&frame_table.lock);
      f->eax = -1;
      return;
    }
  }

  // select mmap id;
  mapid_t mapid;
  if (list_empty(&p->mmap_list)) {
    mapid = 0;
  } else {
    struct list_elem *e = list_back(&p->mmap_list);
    struct mmap *back = list_entry(e, struct mmap, elem);
    mapid = back->mapid + 1;
  }

  // create mmap structure
  struct mmap *mmap = (struct mmap *)malloc(sizeof(struct mmap));
  mmap->mapid = mapid;
  mmap->page = page;
  mmap->file = file_reopen(p->files[fd]);

  // push back
  list_push_back(&p->mmap_list, &mmap->elem);

  // page talbe mapping
  for (i=0; i<len / PGSIZE + 1; i++) {
    page_table_insert_file(&p->page_table, page + i * PGSIZE, mmap->file, i * PGSIZE);
  }

  lock_release(&lock_file);
  lock_release(&frame_table.lock);

  f->eax = mapid;
  return;
}

void sys_munmap(struct intr_frame *f) {
  mapid_t mapid = get_integer(f->esp, 1);

  #ifdef DEBUG
  printf("[sys_munmap] mapid: %u\n", mapid);
  #endif

  if (!mmap_find(mapid)) {
    return;
  }

  lock_acquire(&frame_table.lock);
  lock_acquire(&lock_file);

  mmap_write_back(mapid);
  mmap_free(mapid);

  lock_release(&lock_file);
  lock_release(&frame_table.lock);

  return;
}

#endif

#ifdef PR_FS

void sys_chdir(struct intr_frame *f) {
  const char *name = (const char *)get_pointer(f->esp, 1);

  #ifdef DEBUG
  printf("[sys_chdir] name: %s\n", name);
  #endif

  char *temp = (char *)malloc(sizeof(char) * (NAME_MAX + 1));
  strlcpy(temp, name, strlen(name) + 1);

  struct dir *dir;
  struct inode *inode;
  char *final;

  if (name[0] == '/') {
    // absolute path
    dir = dir_open_root();
    inode = dir->inode;
  } else {
    // relative path
    dir = process_current_dir();
    inode = dir->inode;
  }

  dir = filesys_find_dir(dir, inode, temp, &final);
  if (!dir) {
    // couldn't find parent
    free(temp);
    f->eax = 0; // return false
    return;
  }

  // final search
  if (!dir_lookup(dir, final, &inode)) {
    // final search failed
    dir_close(dir);
    free(temp);
    f->eax = 0;
    return;
  }
  dir_close(dir);

  if (!inode->data.is_dir) {
    free(temp);
    f->eax = 0;
    return;
  }

  if (inode == process_current_dir()->inode) {
    // found current Directory
    free(temp);
    f->eax = 1;
    return;
  }

  struct dir *chdir = dir_open(inode);

  // change current directory
  dir_close(process_current_dir());
  process_current()->dir = chdir;
  free(temp);
  f->eax = 1;
}

void sys_mkdir(struct intr_frame *f) {
  const char *name = (const char *)get_pointer(f->esp, 1);

  #ifdef DEBUG
  printf("[sys_mkdir] name: %s\n", name);
  #endif

  if (!strcmp(name, "")) {
    // empty name
    f->eax = 0;
    return;
  }
  char *temp = (char *)malloc(sizeof(char) * (NAME_MAX + 1));
  strlcpy(temp, name, strlen(name) + 1);

  struct dir *dir;
  struct inode *inode;
  char *final;

  if (name[0] == '/') {
    // absolute path
    dir = dir_open_root();
    inode = dir->inode;
  } else {
    // relative path
    dir = process_current_dir();
    inode = dir->inode;
  }

  dir = filesys_find_dir(dir, inode, temp, &final);
  if (!dir) {
    // couldn't find parent
    free(temp);
    f->eax = 0; // return false
    return;
  }

  // final search
  if (dir_lookup(dir, final, &inode)) {
    // if already exists, fail to create
    dir_close(dir);
    free(temp);
    f->eax = 0;
    return;
  }

  // final creation
  disk_sector_t inode_sector = 0;
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && dir_create(inode_sector, 0, dir->inode->sector) // create 0 entries
                  && dir_add (dir, final, inode_sector));
  if (!success && inode_sector != 0)
    free_map_release (inode_sector, 1);
  dir_close (dir);
  free(temp);

  f->eax = success;
}

void sys_readdir(struct intr_frame *f) {
  int fd = get_integer(f->esp, 1);
  char *name = (char *)get_pointer(f->esp, 2);

  #ifdef DEBUG
  printf("[sys_readdir] fd:%d, name: %s\n", fd, name);
  #endif

  if (fd == STDIN_FILENO || fd == STDOUT_FILENO || !process_valid_fd(fd)) {
    f->eax = 0;
    return;
  }

  struct process *p = process_current();
  if (p->files[fd]->inode->data.is_dir) {
    f->eax = dir_readdir((struct dir*)p->files[fd], name);
  } else {
    f->eax = 0;
  }
}

void sys_isdir(struct intr_frame *f) {
  int fd = get_integer(f->esp, 1);

  #ifdef DEBUG
  printf("[sys_isdir] fd:%d\n", fd);
  #endif

  if (fd == STDIN_FILENO || fd == STDOUT_FILENO || !process_valid_fd(fd)) {
    f->eax = 0;
    return;
  }

  struct process *p = process_current();
  if (p->files[fd]->inode->data.is_dir) {
    f->eax = 1;
  } else {
    f->eax = 0;
  }
}

void sys_inumber(struct intr_frame *f) {
  int fd = get_integer(f->esp, 1);

  #ifdef DEBUG
  printf("[sys_inumber] fd:%d\n", fd);
  #endif

  if (fd == STDIN_FILENO || fd == STDOUT_FILENO || !process_valid_fd(fd)) {
    f->eax = 0;
    return;
  }

  struct process *p = process_current();
  f->eax = p->files[fd]->inode->sector;
}

#endif
#endif

static void
syscall_handler (struct intr_frame *f) // UNUSED?
{

  #ifdef PR_USER
  // System call infrastructure

  #ifdef PR_VM
  process_current()->esp = f->esp;
  #endif

  void *esp = f->esp;
  if (!is_valid(esp, esp)) {
    exit_status(PID_ERROR);
  }

  uint32_t syscall_number = *((uint32_t *)esp);

  void (*handler[SYSCALL_COUNT]) (struct intr_frame *f) = {
    sys_halt,
    sys_exit,
    sys_exec,
    sys_wait,
    sys_create,
    sys_remove,
    sys_open,
    sys_filesize,
    sys_read,
    sys_write,
    sys_seek,
    sys_tell,
    sys_close,
    #ifdef PR_VM
    sys_mmap,
    sys_munmap,
    #endif
    #ifdef PR_FS
    sys_chdir,
    sys_mkdir,
    sys_readdir,
    sys_isdir,
    sys_inumber,
    #endif
  };

  #ifdef DEBUG
  printf("system call number: %d\n", syscall_number);
  #endif

  handler[syscall_number](f);

  #else
  printf ("system call!\n");
  thread_exit ();
  #endif

}
