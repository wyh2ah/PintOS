#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "list.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"

static void syscall_handler (struct intr_frame *);

static struct lock filesys_lock;

/* In order to support filesys operations in process.c
    we need to create an api for the filesys_lock */
void filesys_lock_ac_api() { lock_acquire(&filesys_lock); }
void filesys_lock_re_api() { lock_release(&filesys_lock); }


/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
static int
get_user (const uint8_t *uaddr)
{
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
       : "=&a" (result) : "m" (*uaddr));
  return result;
}
 
/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
static bool
put_user (uint8_t *udst, uint8_t byte)
{
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
       : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}


/* This is used to check the validity of the input str according
  to the check size. If there is no specific size, you should call
  check_valid_str */
void
check_valid(const void * ptr, unsigned size)
{
  struct thread * current_thread = thread_current();
  unsigned * ptr_cpy = ptr;

  for(int i = 0; i < size; i++){
    if(!is_user_vaddr(ptr_cpy + i)){
      current_thread->exit_code = -1;
      thread_exit();
    }

    if(!pagedir_get_page(current_thread->pagedir, ptr_cpy + i)){
      current_thread->exit_code = -1;
      thread_exit();
    }
  }
}

/* Used to check str that has uncertain size */
void
check_valid_str(const char * str)
{ 
  char * str_cpy = str;
  while(true){
    check_valid(str_cpy, 1);
    if (*str_cpy == '\0') break;
    str_cpy++;
  }
}


/* Used to check specific buffer in read and write */
void
check_valid_buffer(void * buffer, unsigned int size){
  for (unsigned int i = 0; i<size; i++){
    if(!is_user_vaddr (buffer+i)){
      thread_current()->exit_code = -1;
      thread_exit();
    }

    if(!pagedir_get_page (thread_current()->pagedir, buffer+i)){
      thread_current()->exit_code = -1;
      thread_exit();
    }
  }
}


void
syscall_init (void) 
{
  lock_init (&filesys_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

void 
syscall_halt(void)
{
  shutdown_power_off();
}

void 
syscall_exit(int status)
{ 
  struct thread * current_thread = thread_current();
  current_thread->exit_code = status;
  thread_exit();
}

tid_t 
syscall_exec (const char * cmd_line)
{
  return process_execute(cmd_line);
}

int 
syscall_wait (tid_t pid)
{
  return process_wait(pid);
}

bool 
syscall_create (const char *file, unsigned initial_size)
{
  lock_acquire(&filesys_lock);
  bool ret = filesys_create(file, initial_size);
  lock_release(&filesys_lock);
  return ret;
}

bool 
syscall_remove (const char *file)
{
  lock_acquire(&filesys_lock);
  bool ret = filesys_remove(file);
  lock_release(&filesys_lock);
  return ret;
}

int 
syscall_open (const char *file)
{
  lock_acquire(&filesys_lock);
  struct file * f = filesys_open(file);
  lock_release(&filesys_lock);
  if (f == NULL) return -1;
  struct file_node * f_node = (struct file_node *) malloc (sizeof (struct file_node));
  struct thread * current_thread = thread_current();
  current_thread->fd++;
  f_node->fd = current_thread->fd;
  f_node->file_f = f;
  if (inode_is_dir (file_get_inode (f)))
      f_node->dir_ptr = dir_open (file_get_inode (f));
    else
      f_node->dir_ptr = NULL;
  list_push_back(&current_thread->files, &f_node -> elem);
  return (f_node->fd);
}

int 
syscall_filesize (int fd)
{
  struct file_node* f_node = search_fd(&thread_current()->files, fd, false);
  if(f_node != NULL){
    lock_acquire (&filesys_lock);
    int result = file_length(f_node->file_f);
    lock_release (&filesys_lock);
    return result;
  }else{
    return -1;
  }
}

int 
syscall_read (int fd, void *buffer, unsigned size)
{ 
  int ret = -1;
  if(fd == STDIN_FILENO){
    for (int i = 0; i < size; i++) ((char*)buffer)[i] = input_getc();
    return size;
  } else if (fd != STDOUT_FILENO){
    struct file_node * f_node = search_fd(&thread_current()->files, fd, false);
    if(f_node != NULL){
      lock_acquire(&filesys_lock);
      ret = file_read (f_node->file_f, buffer, size);
      lock_release(&filesys_lock);
    }
  }
  return ret;
}

int
syscall_write (int fd, const void *buffer, unsigned size)
{ 
  int ret = -1;
  if (fd == STDOUT_FILENO){
    putbuf(buffer, size);
    return size;
  } else if (fd != STDIN_FILENO){
    struct file_node * f_node = search_fd(&thread_current ()->files, fd, false);
    if(f_node != NULL){
      lock_acquire(&filesys_lock);
      ret = file_write(f_node->file_f, buffer, size);
      lock_release(&filesys_lock);
    }
  }
  return ret;
}

void 
syscall_seek (int fd, unsigned position)
{
  struct file_node * f_node = search_fd(&thread_current()->files, fd, false);
  if (f_node == NULL){
    thread_current()->exit_code = -1;
    thread_exit();
  }
  file_seek(f_node->file_f, position);
}

unsigned 
syscall_tell (int fd)
{
  struct file_node * f_node = search_fd(&thread_current()->files, fd, false);
  if (f_node == NULL) return -1;

  lock_acquire(&filesys_lock);
  int32_t ret = file_tell(f_node->file_f);
  lock_release(&filesys_lock);
  
  return ret;
}

void 
syscall_close (int fd)
{
  struct file_node * f_node = search_fd (&thread_current ()->files, fd, true);
  if (f_node == NULL) return -1;

  lock_acquire(&filesys_lock);
  file_close (f_node -> file_f);
  lock_release(&filesys_lock);

  list_remove (&f_node->elem);
  free (f_node);
}

bool
syscall_chdir (const char * dir){
  lock_acquire (&filesys_lock);
  bool ret = filesys_chdir (dir);
  lock_release (&filesys_lock);
  return ret;
}

bool
syscall_mkdir (const char * dir){
  lock_acquire (&filesys_lock);
  bool ret = filesys_mkdir (dir);
  lock_release (&filesys_lock);
  return ret;
}

bool
syscall_readdir (int fd, char *name){
  lock_acquire (&filesys_lock);
  bool ret = filesys_readdir (fd, name);
  lock_release (&filesys_lock);
  return ret;
}

bool 
syscall_isdir (int fd){
  lock_acquire (&filesys_lock);
  bool ret = filesys_isdir (fd);
  lock_release (&filesys_lock);
  return ret;
}

int 
syscall_inumber (int fd){
  lock_acquire (&filesys_lock);
  int ret = filesys_inumber (fd);
  lock_release (&filesys_lock);
  return ret;
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  struct thread * cur = thread_current();

  check_valid(f->esp, 4);

  switch (*(int*)f->esp)
  {
    /* Terminates Pintos by calling shutdown_power_off() */
    case SYS_HALT:
    {
      syscall_halt();
      break;
    }

    /* Terminates the current user program, returning status to the kernel */
    case SYS_EXIT:
    {
      syscall_exit(*((int*)f->esp + 1));
      break;
    }
    
    /* Runs the executable whose name is given in cmd_line, 
       passing any given arguments, and returns the new 
       process's program id (pid) */
    case SYS_EXEC:
    {
      char * cmd_line = (char *)(*((int*)f->esp + 1));
      check_valid_str(cmd_line);
      f->eax = syscall_exec(cmd_line);
      break;
    }

    /* Waits for a child process pid and retrieves the child's exit status */
    case SYS_WAIT:
    {
      pid_t pid = (*((int*)f->esp + 1));
      f->eax = syscall_wait(pid);
      break;
    }

    /* Creates a new file called file initially initial_size bytes in size.
       Returns true if successful, false otherwise.  */
    case SYS_CREATE:
    {
      char *file = (char *)(*((int*)f->esp + 1));
      unsigned initial_size = *((unsigned*)f->esp + 2);
      check_valid_str(file);
      f->eax = syscall_create(file, initial_size);
      break;
    }

    /* Deletes the file called file. 
       Returns true if successful, false otherwise */
    case SYS_REMOVE:
    {
      char *file = (char *)(*((int*)f->esp + 1));
      check_valid_str(file);
      f->eax = syscall_remove(file);
      break;
    }

    /* Opens the file called file. Returns file descriptor, 
       or -1 if the file could not be opened. */
    case SYS_OPEN:
    {
      char *file = (char *)(*((int*)f->esp + 1));

      check_valid_str(file);
      f->eax = syscall_open(file);
      break;
    }

    /* Returns the size, in bytes, of the file open as fd. */
    case SYS_FILESIZE:
    {
      int fd = *((int*)f->esp + 1);
      f->eax = syscall_filesize(fd);
      break;
    }

    /* Reads size bytes from the file open as fd into buffer. Returns the 
       number of bytes actually read (0 at end of file), or -1 if the 
       file could not be read (due to a condition other than end of file).
       Fd 0 reads from the keyboard using input_getc(). */
    case SYS_READ:
    {
      int fd = *((int*)f->esp + 1);
      void* buffer = (void*)(*((int*)f->esp + 2));
      unsigned size = *((unsigned*)f->esp + 3);

      check_valid_buffer(buffer, size);
      f->eax = syscall_read(fd, buffer, size);
      break;
    }

    /* Writes size bytes from buffer to the open file fd. Returns the 
       number of bytes actually written, which may be less than size 
       if some bytes could not be written. */
    case SYS_WRITE:
    {
      int fd = *((int*)f->esp + 1);
      void* buffer = (void*)(*((int*)f->esp + 2));
      unsigned size = *((unsigned*)f->esp + 3);

      check_valid_buffer(buffer, size);
      f->eax = syscall_write(fd, buffer, size);
      break;
    }

    /* Changes the next byte to be read or written in open file fd to 
       position, expressed in bytes from the beginning of the file. 
       (Thus, a position of 0 is the file's start.) */
    case SYS_SEEK:
    {
      int fd = *((int*)f->esp + 1);
      unsigned position = *((unsigned*)f->esp + 2);
      lock_acquire (&filesys_lock);
      syscall_seek(fd, position);
      lock_release (&filesys_lock);
      break;
    }
    
    /* Returns the position of the next byte to be read or written 
       in open file fd, expressed in bytes from the beginning of the file. */
    case SYS_TELL:
    {
      int fd = *((int*)f->esp + 1);
      f->eax = syscall_tell(fd);
      break;
    }

    /* Closes file descriptor fd. Exiting or terminating a process implicitly 
       closes all its open file descriptors, as if by calling this function 
       for each one. */
    case SYS_CLOSE:
    {
      int fd = *((int*)f->esp + 1);
      syscall_close(fd);
      break;
    }

    case SYS_CHDIR: 
    {
      char * dir = (char *)(*((int*)f->esp + 1));
      check_valid_str(dir);
      f->eax = syscall_chdir(dir);
      break;
    }

    case SYS_MKDIR:
    {
      char * dir = (char *)(*((int*)f->esp + 1));

      check_valid_str(dir);
      f->eax = syscall_mkdir(dir);
      break;
    }

    case SYS_READDIR: 
    {
      int fd = *((int*)f->esp + 1);
      char * name = (void*)(*((int*)f->esp + 2));

      check_valid_str(name);
      f->eax = syscall_readdir(fd, name);
      break;
    }

    case SYS_ISDIR:
    {
      int fd = *((int*)f->esp + 1);
      f->eax = syscall_isdir(fd);
      break;
    }

    case SYS_INUMBER:
    {
      int fd = *((int*)f->esp + 1);
      f->eax = syscall_inumber(fd);
      break;
    }

    default:
      thread_current()->exit_code = -1;
      thread_exit();
      break;
  }
}

