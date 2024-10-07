#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/palloc.h"

struct file_control_block
{
    void * file_name;
    struct semaphore sema;
    bool success;
};


tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

typedef int pid_t;

#endif /* userprog/process.h */