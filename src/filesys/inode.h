#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"
#include "filesys/inode.h"
#include <stdio.h>
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/cache.h"

struct bitmap;

#define INODE_DIRECT_N 8
#define INODE_INDIRECT_N 116

#define DIRECT_PTR_NUM 8
#define INDIRECT_PTR_NUM 116

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    block_sector_t direct_blocks[INODE_DIRECT_N];
  
    block_sector_t indirect_blocks[INODE_INDIRECT_N];
     block_sector_t double_indirect_blocks;

    bool is_dir;

    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
  };


/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise.*/
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
  };

void inode_init (void);
bool inode_create (block_sector_t, off_t);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
block_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);

bool inode_is_dir (const struct inode *);

#endif /* filesys/inode.h */