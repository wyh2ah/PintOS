#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);

void filesys_lock_ac_api();
void filesys_lock_re_api();

#endif /* userprog/syscall.h */