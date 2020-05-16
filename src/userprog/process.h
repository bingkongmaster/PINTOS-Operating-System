#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

/**
 * PROJECT_THREAD, PR_USER, DEBUG macros are defined in <debug.h>
 */
#ifdef PR_USER

#include "threads/synch.h"
#include "filesys/filesys.h"
#include "filesys/file.h"

#ifdef PR_VM
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"
#endif

#define DELIM " "
#define MAX_ARG_COUNT 32
#define MAX_FILE_COUNT 128
#define MIN_FILE_COUNT 2
#define PID_ERROR -1

typedef int pid_t;
typedef int mapid_t;

/**
* mmap structure
*/
struct mmap {
  mapid_t mapid;
  uint8_t *page;
  struct file *file;
  struct list_elem elem;
};

/**
 * process structure
 *
 * Higher level abstraction in addition to threads.
 * Process structure is dedicated to describe user program execution.
 *
 * Since a parent process needs to wait for its children,
 * it stores a list of children and tracks their states.
 *
 * Value of pid(process id) is same as tid(thread id).
 *
 * File name and arguments are copied and stored per-process.
 * Locks and conditional variables used for synchronizations are per-process.
 * There are no global variables or states describing process behavior.
 *
 * Process structure does not get destroyed even if the process exits.
 * It gets destroyed after its parent calls wait system call appropriately.
 */
struct process {
    struct list children;
    struct list_elem elem;

    char *name;
    int argc;
    char *argv[MAX_ARG_COUNT];

    bool success; // indicates whether if process creation(loading) was successful or not
    // load <-> lock_exec <-> cond_load_done
    bool load; // indicates whether if loading is over(regardless of success)
    // exit <-> lock_wait <-> cond_exit
    bool exit; // indicates whether if process exited.

    pid_t pid;
    int status; // exit status
    struct thread *thread;

    struct file *files[MAX_FILE_COUNT];
    struct file *exec; // ELF file the process is currently executing

    struct lock lock_exec;
    struct lock lock_wait;
    struct condition cond_load_done;
    struct condition cond_exit;

    #ifdef PR_VM
    struct page_table page_table;
    void *esp; // esp stored when switched from user mode to kernel mode

    struct list mmap_list;
    #endif

    #ifdef PR_FS
    struct dir *dir; // current directory
    #endif
};

void process_parse(void *file_name);
struct process *process_create(tid_t tid);
struct process *process_current(void);
pid_t process_pid(void);
struct process *process_find_child(pid_t child_pid);
void process_reap(struct process *child);
int process_open_file(struct file *file);
void process_close_file(int fd);
bool process_valid_fd(int fd);

#ifdef PR_VM
void process_table_free(void);
struct mmap *mmap_find(mapid_t mapid);
void mmap_write_back(mapid_t mapid);
void mmap_free(mapid_t mapid);
#endif

#ifdef PR_FS
struct dir *process_current_dir(void);
#endif

#ifdef DEBUG
void process_print(void);
#endif

#endif

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

bool install_page(void *upage, void *kpage, bool writable);
#endif /* userprog/process.h */
