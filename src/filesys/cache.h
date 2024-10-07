#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "devices/block.h"
#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "threads/synch.h"
#include "string.h"
#include <stdio.h>
#include "threads/thread.h"
#include "devices/timer.h"
#include "threads/malloc.h"

#define MAX_CACHE_SIZE 64

/* For read ahead */
struct condition read_ahead_cond;
struct lock read_ahead_lock;
struct list read_ahead_list;

struct buffer_cache
{
    unsigned char buffer[BLOCK_SECTOR_SIZE];
    block_sector_t sector_id;
    bool dirty;
    bool pin_bit;
    bool valid;
};

struct read_ahead
{
    block_sector_t block_to_read;
    void * buffer;
    struct list_elem elem;
};

void cache_init ();
void cache_read (block_sector_t sector_id, void *buffer);
void cache_write (block_sector_t sector_id, void *buffer);
void cache_out_all ();
int search_sector (block_sector_t sector_id);
int clock_algorithm ();

#endif 