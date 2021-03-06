#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "userprog/exception.h"
#include "vm/sup_page.h"
#include "vm/frame.h"

int numOpenFiles=0;//used to assign the id of open file
struct oFiles_elem
{
  struct file * file_fd;
  int num_fd;
  struct list_elem elem;
};
struct write_args {
int num;
int fd;
const void *buffer;
unsigned length;
};
struct create_args {
int num;
const char *file;
unsigned initial_size;
};
struct open_args {
int num;
const char *file;
};
struct close_args {
int num;
int fd;
};
struct read_args {
int num;
int fd;
void *buffer;
unsigned size;
};

static void syscall_handler (struct intr_frame *);
static void invalid_access();
static void my_exit();

struct list oFiles;//files that are open
struct lock file_lock;
void syscall_init (void)
{
  lock_init(&file_lock);
  list_init(&oFiles);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* Returns true if the user address sequence is in valid range or false otherwise.
   If exact is true, the whole range is checked, otherwise this can be used to
   check for validity of strings - it only looks up to end of string \0.
 */
bool validate_user_addr_range(uint8_t *va, size_t bcnt, uint32_t* esp, bool exact);

/* Uses the second technique mentioned in pintos doc. 3.1.5
   to cause page faults and check addresses (returns -1 on fault) */

/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
static int
get_user (const uint8_t *uaddr) {
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:" : "=&a" (result) : "m" (*uaddr));
  return result;
}

/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */

static bool
put_user (uint8_t *udst, uint8_t byte) {
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:" : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}

/* Used to validate pointers, buffers and strings.
   With exact false, it validates until 0 (end of string). */
bool validate_user_addr_range(uint8_t *va, size_t bcnt, uint32_t* esp, bool exact) {
  if(va == NULL)  /* NULL is not allowed */
    return false;
  for(size_t i=0; (i<bcnt) || !exact; i++) {
    if(!is_user_vaddr(va+i)) /* outside user space! wrong */
      return false;
    int w = get_user(va+i);
    if(!exact && w == 0) /* end of string */
      return true;
    if(w == -1) { /* outside mapped pages */
#ifdef VM
      uint8_t* uaddr = PFNO_TO_ADDR(ADDR_TO_PFNO(va+i));
      struct sup_page* spg = lookup_page(uaddr);
      if(spg != NULL && load_page(spg, uaddr)) /* page must be loaded */
        continue; /* check next address */
      if(va+i > (uint8_t*)esp && grow_stack(uaddr)) /* 1st stack access in syscall */
        continue; /* check next address*/
      /* none of these situations! */
#endif
      return false;
    }
  }
  return true;
}

/* File system primitive synchronization. Sequentialize file system accesses. */
#define FS_ATOMIC(code) \
  {  fs_take(); \
     {code} \
     fs_give(); }

static void
syscall_handler (struct intr_frame *f)
{
  int* NUMBER= (int*) ((f->esp));
  // printf("syscall.c-122 syscall number %d\n", *NUMBER);
  switch (*NUMBER) {
    case SYS_WRITE:
      {
        struct write_args *args = (struct write_args *) f->esp;
        if (args->fd == STDOUT_FILENO)
        {
          if(!validate_user_addr_range(args->buffer,args->length, f->esp, false))
            {
              invalid_access();
            }
          putbuf(args->buffer, args->length);
          f->eax= args->length;
        }
        else
        {
          struct list_elem *e;
          struct file *file_fd;
          int found_file=0;
          struct oFiles_elem *fof;
          for (e = list_begin (&oFiles); e != list_end (&oFiles);
               e = list_next (e))
            {
              // printf("196\n");
              fof = list_entry (e, struct oFiles_elem, elem);
              if (fof->num_fd==args->fd) {
                file_fd=fof->file_fd;
                found_file=1;
                break;
              }
            }
          if(found_file==0)
          {
            f->eax=-1;
            break;
            // printf("Error Trying to write a file which might not be open. could not find file descriptor. exiting\n" );
            // my_exit();
          }
          lock_acquire(&file_lock);
          f->eax=file_write(file_fd, args->buffer, args->length);
          lock_release(&file_lock);
        }
        break;
      }
    case SYS_CREATE:
      {
        struct create_args *args = (struct create_args *) f->esp;
        if(!validate_user_addr_range(args->file,1, f->esp, false))//just look at the first address
          {
            invalid_access();
          }
        f->eax=filesys_create(args->file, args->initial_size);
        break;
      }
    case SYS_EXIT:
      {
        my_exit();
        // break;
      }
    case SYS_HALT:
      {
        shutdown_power_off();
      }
    case SYS_OPEN:
      {
        struct open_args *args = (struct open_args *) f->esp;
        if(!validate_user_addr_range(args->file,1, f->esp, false))//just look at the first address
          {
            invalid_access();
          }
        lock_acquire(&file_lock);
        struct file * file_fd= filesys_open(args->file);
        if (!file_fd) {
          lock_release(&file_lock);
            f->eax=-1;
            break;
        }
        struct oFiles_elem* oe=palloc_get_page(PAL_USER);
        oe->file_fd=file_fd;
        oe->num_fd=numOpenFiles+3;//does no reclaimation. 0,1,2 are for stdin, stdout, stderr so start from 3
        list_push_back(&oFiles,&(oe->elem) );
        lock_release(&file_lock);
        // oe.
        numOpenFiles++;
        f->eax=oe->num_fd;
        break;
      }
    case SYS_CLOSE:
      {
        struct close_args *args = (struct close_args *) f->esp;
        struct list_elem *e;
        struct file *file_fd;
        int found_file=0;
        struct oFiles_elem *fof;
        for (e = list_begin (&oFiles); e != list_end (&oFiles);
             e = list_next (e))
          {
            // printf("196\n");
            fof = list_entry (e, struct oFiles_elem, elem);
            if (fof->num_fd==args->fd) {
              file_fd=fof->file_fd;
              found_file=1;
              break;
            }
          }
        if(found_file==0)
        {
          printf("Error Trying to close a file which might not be open. could not find file descriptor. exiting\n" );
          my_exit();
        }
        lock_acquire(&file_lock);
        file_close(file_fd);
        list_remove(&(fof->elem));
        palloc_free_page(fof);
        lock_release(&file_lock);
        break;
      }
    case SYS_READ:
      {
        struct read_args *args = (struct read_args *) f->esp;
        if (!validate_user_addr_range(args->buffer,args->size,f->esp,false)) {
          printf("Invalid access in sys call read. Exiting\n");
          invalid_access();
        }
        lock_acquire(&file_lock);
        if (args->fd==STDIN_FILENO)
        {
          void *buffer=args->buffer;
          for (size_t i = 0; i < args->size; i++)
          {
            if(!put_user(buffer+i, input_getc()))
            {
              //some error
              printf("Error in taking input from console exiting\n" );
              lock_release(&file_lock);
              my_exit();
            }
          }
          lock_release(&file_lock);
          f->eax=args->size;
          break;
        }
        struct list_elem *e;
        struct file *file_fd;
        int found_file=0;
        for (e = list_begin (&oFiles); e != list_end (&oFiles);
             e = list_next (e))
          {
            // printf("196\n");
            struct oFiles_elem *fof = list_entry (e, struct oFiles_elem, elem);
            if (fof->num_fd==args->fd) {
              file_fd=fof->file_fd;
              found_file=1;
              break;
            }
          }
        if(found_file==0)
        {
          // printf("Error Trying to read a file which might not be open. could not find file descriptor. exiting\n" );
          // my_exit();
          f->eax=-1;
        }
        else
        {
          f->eax=file_read(file_fd,args->buffer, args->size);
        }
        lock_release(&file_lock);
        break;
      }
    case SYS_REMOVE:
      {
          struct open_args *args = (struct open_args *) f->esp;
          if (!validate_user_addr_range(args->file,1,f->esp,false)) {
            printf("Invalid access in sys call remove. Exiting\n");
            invalid_access();
          }
          lock_acquire(&file_lock);
          f->eax=filesys_remove(args->file);
          lock_release(&file_lock);
          break;
        }
    default:
    {
        printf("System calls not implemented.\n");
        thread_exit();
    }
  }

}

static void invalid_access()
{
  if (lock_held_by_current_thread(&file_lock))
  {
    lock_release(&file_lock);
  }
  my_exit();
}
void my_exit()
{
  thread_exit();
}
