#include "filesys/cache.h"
#include <debug.h>


static struct buffer_cache cache[MAX_CACHE_SIZE]; 
struct lock cache_lock;
int clock_ptr;


void 
write_behind()
{
    while (true){
        timer_sleep (1000);
        cache_out_all ();
    }
}

void 
read_ahead()
{
    while(true){
        cond_wait(&read_ahead_cond, &read_ahead_lock);
        struct read_ahead * read_ahead_unit = list_entry(list_pop_front(&read_ahead_list), struct read_ahead, elem);
        cache_read(read_ahead_unit->block_to_read, read_ahead_unit->buffer);
    }
}

void 
cache_init ()
{
    lock_init(&cache_lock);
    lock_init(&read_ahead_lock);
    cond_init(&read_ahead_cond);
    list_init(&read_ahead_list);
    clock_ptr = -1;
    for (int i = 0; i < MAX_CACHE_SIZE; i++){
        memset (cache[i].buffer, 0, BLOCK_SECTOR_SIZE);
        cache[i].sector_id = 0;
        cache[i].dirty = false;
        cache[i].valid = false;
        cache[i].pin_bit = false;
    }

    thread_create ("write_behind_t", PRI_DEFAULT, write_behind, NULL);
    // thread_create ("read_ahead_t", PRI_DEFAULT, read_ahead, NULL);
}

void
cache_read (block_sector_t sector_id, void *buffer)
{
    lock_acquire(&cache_lock);
    int slot = search_sector (sector_id);
    if (slot != -1){
        memcpy (buffer, cache[slot].buffer, BLOCK_SECTOR_SIZE);
    }else {
        int slot = clock_algorithm ();
        cache[slot].sector_id = sector_id;
        cache[slot].dirty == false;
        cache[slot].valid = true;
        cache[slot].pin_bit = true;

        block_read (fs_device, sector_id, cache[slot].buffer);
        memcpy (buffer, cache[slot].buffer, BLOCK_SECTOR_SIZE);
    }
    lock_release(&cache_lock);
}

void
cache_write (block_sector_t sector_id, void *buffer)
{
    lock_acquire(&cache_lock);
    int slot = search_sector (sector_id);
    if (slot != -1){
        cache[slot].pin_bit = true;

        memcpy (cache[slot].buffer, buffer, BLOCK_SECTOR_SIZE);
        cache[slot].dirty = true;
    }else {
        int slot = clock_algorithm ();

        cache[slot].sector_id = sector_id;
        cache[slot].valid = true;
        cache[slot].dirty = true;
        cache[slot].pin_bit = true;

        /* Write back is in clock algorithm */
        memcpy (cache[slot].buffer, buffer, BLOCK_SECTOR_SIZE);
    }
    lock_release(&cache_lock);
    block_write (fs_device, sector_id, buffer);
}


int search_sector (block_sector_t sector_id)
{
    for (int i = 0; i < MAX_CACHE_SIZE; i++)
        if (cache[i].valid && cache[i].sector_id == sector_id) return i;
        
    return -1;
}

int clock_algorithm ()
{
    // clock
    while (true){
        /* We first check whether there are cache block that is marked free and if so we directly release that cache block*/
        if (!cache[clock_ptr].valid){
            int ret = clock_ptr;
            clock_ptr_move();
            return ret;
        }
        /* We pick the one that doesn't have a second chance */
        if (!cache[clock_ptr].pin_bit){
            if (cache[clock_ptr].dirty) block_write(fs_device, cache[clock_ptr].sector_id, cache[clock_ptr].buffer);
            cache[clock_ptr].valid = false;
            int ret = clock_ptr;
            clock_ptr_move();
            return ret;
        }

        cache[clock_ptr].pin_bit = false;
        clock_ptr_move();
    }


    // LRU to fix
    // int ret = -1;
    // remake:
    // for(int i = 0; i < 64; i++){
    //     if(cache[i].used) continue;
    //     int minimum = 0x3f3f3f3f;
    //     if(cache[i].accessed < minimum){
    //         if(cache[i].pin_bit){
    //             cache[i].pin_bit = false;
    //             continue;
    //         }
    //         minimum = cache[i].pin_bit;
    //         ret = i;
    //     }
    // }

    // if(ret == -1) goto remake;
    // if(cache[ret].dirty) block_write (fs_device, cache[ret].sector_id, cache[ret].buffer);
    // return ret;
}

int
clock_ptr_move()
{
    clock_ptr = (clock_ptr + 1) % MAX_CACHE_SIZE;
    return;
}


void cache_out_all ()
{
    lock_acquire(&cache_lock);
    for (int i = 0; i < MAX_CACHE_SIZE; i++) {
        if (cache[i].valid && cache[i].dirty){
            block_write (fs_device, cache[i].sector_id, cache[i].buffer);
            cache[i].valid = false;
            cache[i].dirty = false;
        }
    }
    lock_release(&cache_lock);
}
